#include <linux/dma-fence-array.h>
#include <linux/interval_tree_generic.h>
#include <linux/idr.h>

#include "opu_vm_mgr.h"
#include "opu.h"

#define START(node) ((node)->start)
#define LAST(node) ((node)->last)

INTERVAL_TREE_DEFINE(struct amdgpu_bo_va_mapping, rb, uint64_t, __subtree_last,
		     START, LAST, static, amdgpu_vm_it)

#undef START
#undef LAST

/**
 * struct amdgpu_prt_cb - Helper to disable partial resident texture feature from a fence callback
 */
struct amdgpu_prt_cb {
	struct amdgpu_device *adev;
	struct dma_fence_cb cb;
};

union opu_pe {
    struct {
        uint64_t    pe_valid        : 1;
        uint64_t    pe_system       : 1;
        uint64_t    reserved1       : 1;
        uint64_t    pe_snooped      : 1;
        uint64_t    pe_executable   : 1;
        uint64_t    pe_readable     : 1;
        uint64_t    pe_writeable    : 1;
        uint64_t    pe_frag         : 4;
        uint64_t    pe_transfurther : 1;
        uint64_t    pe_address      : 36;
        uint64_t    reserved2       : 4;
        uint64_t    pe_resident     : 1;
        uint64_t    pe_pde_as_pte   : 1;
        uint64_t    pe_page_size    : 1;
        uint64_t    pe_frame_level  : 3;
        uint64_t    pe_offset       : 5;
        uint64_t    reserved3       : 1;
    };
    uint64_t u64;
};

const char *opu_pe_level_str[] = {
    "OPU_VM_PD0",
    "OPU_VM_PD1",
    "OPU_VM_PDE",
    "OPU_VM_PTE",
    "OPU_VM_SPE",
};

const char *opu_pe_str[] = {
    "pe_valid",
    "pe_system",
    "reserved1",
    "pe_snooped",
    "pe_executable",
    "pe_readable",
    "pe_writeable",
    "pe_frag",
    "pe_transfurther",
    "pe_address",
    "reserved2",
    "pe_resident",
    "pe_pde_as_pte",
    "pe_page_size",
    "pe_frame_level",
    "pe_offset",
    "reserved3",
};

enum opu_pe_field {
    PE_VALID = 0,
    PE_SYSTEM,
    RESERVED1,
    PE_SNOOPED,
    PE_EXECUTABLE,
    PE_READABLE,
    PE_WRITEABLE,
    PE_FRAG,
    PE_TRANSFURTHER,
    PE_ADDRESS,
    RESERVED2,
    PE_RESIDENT,
    PE_PDE_AS_PTE,
    PE_PAGE_SIZE,
    PE_FRAME_LEVEL,
    PE_OFFSET,
    RESERVED3,
};

const static struct {
    char    *name;
    uint8_t bits;
    uint8_t shift;
} opu_pe_info[] = {
    {"pe_valid",			1, 0 },
    {"pe_system",			1, 1 },
    {"reserved1",			1, 2 },
    {"pe_snooped",			1, 3 },
    {"pe_executable",		1, 4 },
    {"pe_readable",			1, 5 },
    {"pe_writeable",		1, 6 },
    {"pe_frag",			    4, 7 },
    {"pe_transfurther",		1, 11 },
    {"pe_address",			36, 12 },
    {"reserved2",			4, 48 },
    {"pe_resident",			1, 52 },
    {"pe_pde_as_pte",		1, 53 },
    {"pe_page_size",		1, 54 },
    {"pe_frame_level",		3, 55 },
    {"pe_offset",			5, 58 },
    {"reserved3",			1, 63 },
}


int opu_vm_clear_freed_mapping(struct amdgpu_device *adev, uint32_t engine);

/* return addr shift for each level */
static unsigned opu_vm_level_shift(struct opu_device *odev, unsigned level)
{
    unsigned shift = 0;
    int i;
    WARN_ON(level > OPU_VM_MAX_LEVEL);

    for (i = OPU_VM_SPE; i > level; i--)
    {
        shift += odev->vm_mgr.frame_size[i];
    }

    return shift;
}

/* return number of entries in a PD/PT */
static unsigned opu_vm_num_entries(struct opu_device *odev, unsigned level)
{
    unsigned shift = opu_vm_level_shift(odev, odev->vm_mgr.root_level);

    if (level == odev->vm_mgr.root_level)
        return round_up(odev->vm_mgr.max_pfn, 1ULL << shift) >> shift;
    else
        return (1 << odev->vm_mgr.frame_size[level]);
}

/* the mask to get the entry number of a PD/PT */
static uint32_t opu_vm_entries_mask(struct opu_device *odev, unsigned int level)
{
    return ((1 << odev->vm_mgr.frame_size[level]) - 1);
}

static uint64_t opu_vm_va_level_offset(struct opu_device *odev, uint32_t level,
                    uint64_t va)
{
    return ((va >> (OPU_PAGE4K_SHIFT + opu_vm_level_shift(odev, level))) &
            opu_vm_entries_mask(odev, level));
}

static uint64_t opu_vm_get_pe_field(uint64_t pe, enum opu_pe_field field)
{
    return ((pe >> opu_pe_info[field].shift) & ((1ull << opu_pe_info[field].bits) -1));
}

/* return the size of mo in bytes */
static uint32_t opu_vm_mo_size(struct opu_device *odev, uint32_t level)
{
    return PAGE_ALIGN(opu_vm_num_entries(odev, level) * 8);
}

/* get parent page directory */
static uint32_t opu_vm_pt *opu_vm_pt_parent(struct opu_vm_pt *pt)
{
    return pt->parent;
}

/*  state for for_each_opu_vm_pt */
struct opu_vm_pt_cursor {
    uint64_t pfn;
    struct   opu_vm_pt *parent;
    struct   opu_vm_pt *entry;
    unsigned level;
}

/* initiailze the cursor to start a walk */
static void opu_vm_pt_start(struct opu_device *odev,
                    struct opu_vm *vm, uint64_t start, struct opu_vm_pt_cursor *cursor)
{
    cursor->pfn = start;
    cursor->parent = NULL;
    cursor->entry = &vm->root;
    cursor->level = odev->vm_mgr.root_level;
}

/**
 * amdgpu_vm_pt_descendant - go to child node
 *
 * @adev: amdgpu_device pointer
 * @cursor: current state
 *
 * Walk to the child node of the current node.
 * Returns:
 * True if the walk was possible, false otherwise.
 */
static bool opu_vm_pt_descendant(struct opu_device *odev, struct opu_vm_pt_cursor *cursor)
{
    unsigned mask, shift, idx;

    if (!cursor->entry->entries)
        return false;

    mask = opu_vm_entrie_mask(odev, cursor->level);
    shift = opu_vm_level_shift(odev, cursor->level);
    ++cursor->level;

    idx = (cursor->pfn >> shift) & mask;
    cursor->parent = cursor->entry;
    cursor->entry = &cursor->entry->entries[idx];
    return true;
}

/**
 * amdgpu_vm_pt_sibling - go to sibling node
 *
 * @adev: amdgpu_device pointer
 * @cursor: current state
 *
 * Walk to the sibling node of the current node.
 * Returns:
 * True if the walk was possible, false otherwise.
 */
static bool amdgpu_vm_pt_sibling(struct amdgpu_device *adev,
				 struct amdgpu_vm_pt_cursor *cursor)
{
	unsigned shift, num_entries;

	/* Root doesn't have a sibling */
	if (!cursor->parent)
		return false;

	/* Go to our parents and see if we got a sibling */
	shift = amdgpu_vm_level_shift(adev, cursor->level - 1);
	num_entries = amdgpu_vm_num_entries(adev, cursor->level - 1);

	if (cursor->entry == &cursor->parent->entries[num_entries - 1])
		return false;

	cursor->pfn += 1ULL << shift;
	cursor->pfn &= ~((1ULL << shift) - 1);
	++cursor->entry;
	return true;
}


/**
 * amdgpu_vm_pt_ancestor - go to parent node
 *
 * @cursor: current state
 *
 * Walk to the parent node of the current node.
 * Returns:
 * True if the walk was possible, false otherwise.
 */
static bool amdgpu_vm_pt_ancestor(struct amdgpu_vm_pt_cursor *cursor)
{
	if (!cursor->parent)
		return false;

	--cursor->level;
	cursor->entry = cursor->parent;
	cursor->parent = amdgpu_vm_pt_parent(cursor->parent);
	return true;
}

/**
 * amdgpu_vm_pt_next - get next PD/PT in hieratchy
 *
 * @adev: amdgpu_device pointer
 * @cursor: current state
 *
 * Walk the PD/PT tree to the next node.
 */
static void amdgpu_vm_pt_next(struct amdgpu_device *adev,
			      struct amdgpu_vm_pt_cursor *cursor)
{
	/* First try a newborn child */
	if (amdgpu_vm_pt_descendant(adev, cursor))
		return;

	/* If that didn't worked try to find a sibling */
	while (!amdgpu_vm_pt_sibling(adev, cursor)) {
		/* No sibling, go to our parents and grandparents */
		if (!amdgpu_vm_pt_ancestor(cursor)) {
			cursor->pfn = ~0ll;
			return;
		}
	}
}

/**
 * amdgpu_vm_pt_first_dfs - start a deep first search
 *
 * @adev: amdgpu_device structure
 * @vm: amdgpu_vm structure
 * @start: optional cursor to start with
 * @cursor: state to initialize
 *
 * Starts a deep first traversal of the PD/PT tree.
 */
static void amdgpu_vm_pt_first_dfs(struct amdgpu_device *adev,
				   struct amdgpu_vm *vm,
				   struct amdgpu_vm_pt_cursor *start,
				   struct amdgpu_vm_pt_cursor *cursor)
{
	if (start)
		*cursor = *start;
	else
		amdgpu_vm_pt_start(adev, vm, 0, cursor);
	while (amdgpu_vm_pt_descendant(adev, cursor));
}

/**
 * amdgpu_vm_pt_continue_dfs - check if the deep first search should continue
 *
 * @start: starting point for the search
 * @entry: current entry
 *
 * Returns:
 * True when the search should continue, false otherwise.
 */
static bool amdgpu_vm_pt_continue_dfs(struct amdgpu_vm_pt_cursor *start,
				      struct amdgpu_vm_bo_base *entry)
{
	return entry && (!start || entry != start->entry);
}

/**
 * amdgpu_vm_pt_next_dfs - get the next node for a deep first search
 *
 * @adev: amdgpu_device structure
 * @cursor: current state
 *
 * Move the cursor to the next node in a deep first search.
 */
static void amdgpu_vm_pt_next_dfs(struct amdgpu_device *adev,
				  struct amdgpu_vm_pt_cursor *cursor)
{
	if (!cursor->entry)
		return;

	if (!cursor->parent)
		cursor->entry = NULL;
	else if (amdgpu_vm_pt_sibling(adev, cursor))
		while (amdgpu_vm_pt_descendant(adev, cursor));
	else
		amdgpu_vm_pt_ancestor(cursor);
}

/*
 * for_each_amdgpu_vm_pt_dfs_safe - safe deep first search of all PDs/PTs
 */
#define for_each_amdgpu_vm_pt_dfs_safe(adev, vm, start, cursor, entry)		\
	for (amdgpu_vm_pt_first_dfs((adev), (vm), (start), &(cursor)),		\
	     (entry) = (cursor).entry, amdgpu_vm_pt_next_dfs((adev), &(cursor));\
	     amdgpu_vm_pt_continue_dfs((start), (entry));			\
	     (entry) = (cursor).entry, amdgpu_vm_pt_next_dfs((adev), &(cursor)))

static void opu_vm_pe_to_str(uint64_t pe, char *str)
{
    char tmp_str[512];
    char format_str[64];
    uint32_t i;
    for (i = 0; i < PE_MAX_FIELD; i++) {
    }
}

static void dump_table_entry(struct opu_device *odev, struct opu_vm_pt *entry)
{
    int     err;
    uint32_t    cur_level = odev->vm_mgr.root_level;
    uint64_t    cur_pe = 0;
    char        cur_pe_str[p512];
}

void opu_vm_dump(struct opu_device *odev, uint32_t vmid)
{
    struct opu_vm *cur_vm = odev->vm_mgr.id_mgr.ids[vmid].cur_vm;
    struct opu_vm_pt_cursor cursor = {};
    struct opu_vm_pt*       entry = NULL;

    if (!cur_vm) {
        DRM_INFO("Not active vmid");
        return;
    }

    DRM_INFO("Start dump vmid: %u\n", vmid);
    for_each_opu_vm_pt_dfs_safe(odev, cur_vm, NULL, cursor, entry)
        dump_table_entry(odev, entry);

    DRM_INFO("Finish dump vmid: %u\n", vmid);
}

void opu_vm_page_walk(struct opu_device *odev, uint32_t vmid,
                uint64_t vmid_pa, uint64_t va)
{
    uint32_t    cur_level;
    uint64_t    cur_level_base_pa;
    uint64_t    cur_pe_offset;
    uint64_t    cur_pe_pa;
    uint64_t    cur_pe;
    int err;

    struct opu_vm_mgr *vmm = &odev->vm_mgr;
}

struct uint64_t opu_mo_pd_flags(struct opu_device *odev,
                    struct opu_mo *mo, uint32_t level)
{
    uint64_t    flags = OPU_PTE_VALID | OPU_PTE_READABLE |
                        OPU_PTE_WRITEABLE | OPU_PTE_RESIDENT | OPU_PT_FRAME_LEVEL(level);
    if (!mo->is_dev_mem)
        flags |= OPU_PTE_SYSTEM;

    if (!mo->is_cpu_cached)
        flags |= OPU_PTE_SNOOPED;

    if ((level == OPU_VM_PDE) && odev->vm_mgr.vm_caps.use_64k_page)
        flags |= OPU_PTE_PAGE_SIZE;

    if (level == OPU_VM_PTE) {
        flags |= OPU_PTE_TRANSFURTHER | OPU_PTE_PAGE_SIZE;
    }

    return flags;
}

static uint64_t opu_mo_pte_flags(struct opu_device *odev, struct opu_mo *mo, uint32_t access_flags)
{
    uint64_t flags = OPU_PTE_VALID | OPU_PTE_RESIDENT | access_flags;

    if (!mo)
        return flags;

    if (!mo->is_dev_mem) {
        flags |= OPU_PTE_SYSTEM;
    }

    if (mo->is_cpu_cached) {
        flags |= OPU_PTE_SNOOPED;
    }

    return flags;
}

/* Get the entry index which own the shared mo of this entry in parent level,
 * and start offste in bytrs of this entry in shared mo */
static bool opu_vm_get_entry_mo_index(struct opu_device *odev,
                            struct opu_vm_pt *entry,
                            uint32_t level,
                            uint32_t *entry_mo_index,
                            uint32_t *shared_mo_byte_offset)
{
    /* 1. only enable this optimization when 64K PPU page enabled
     */
    if ((!opu_setting_vm_use_offset)        ||
        (!odev->vm_mgr.vm_caps.use_64_page) ||
        (odev->vm_mgr.frame_size[level] >= 9) ||
        (odev->vm_mgr.frame_size[level] < 4)) {
        return false;
    }

    if ((level == OPU_VM_SPE) || (level == OPU_VM_PTE)) {
        /*
         */
        const uint32_t shared_page_bit = 9;
        const uint32_t shared_entry_bit = shared_page_bit - odev->vm_mgr.frame_size[level];
        const uint32_t shared_entry_mask = ((1 << shared_entry_bit) - 1);

        uint32_t cur_entry_index;
        uint32_t mo_entry_index;

        cur_entry_index = entry - entry->parent->entries;
        mo_entry_index = cur_entry_index & ~shared_entry_mask;
        *entry_mo_index = mo_entry_index;
        *shared_mo_byte_offset = ((cur_entry_index - mo_entry_index) *
                    (1 << odev->vm_mgr.frame_size[level])) * 8;
        return true;
    }
    return false;
}

/**
 * amdgpu_vm_clear_bo - initially clear the PDs/PTs
 *
 * @adev: amdgpu_device pointer
 * @vm: VM to clear BO from
 * @vmbo: BO to clear
 * @immediate: use an immediate update
 *
 * Root PD needs to be reserved when calling this.
 *
 * Returns:
 * 0 on success, errno otherwise.
 */
static int amdgpu_vm_clear_bo(struct amdgpu_device *adev,
			      struct amdgpu_vm *vm,
			      struct amdgpu_mo *mo,
			      uint32_t level)
{
	struct amdgpu_vm_update_params params;
	unsigned entries;
	uint64_t addr;
	int r;

	entries = mo->size / 8;
    memset(&params, 0, sizeof(params));
    params.odev = odev;
    params.vm = vm;
    params.is_clear_pt = true;

	r = vm->update_funcs->prepare(&params, NULL);
	if (r)
		return r;

	addr = 0;

	if (entries) {
		uint64_t value = 0, flags = OPU_PT_FRAME_LEVEL(level);

		r = vm->update_funcs->update(&params, mo, addr, 0, ats_entries,
					     value, flags, false);
		if (r)
			return r;

	}

	return vm->update_funcs->commit(&params, NULL);
}

/**
 * amdgpu_vm_update_pde - update a single level in the hierarchy
 *
 * @params: parameters for the update
 * @vm: requested vm
 * @entry: entry to update
 *
 * Makes sure the requested entry in parent is up to date.
 */
static int amdgpu_vm_update_pde(struct amdgpu_vm_update_params *params,
				struct amdgpu_vm *vm,
				struct amdgpu_vm_pt *entry, uint32_t level)
{
    int r;
	struct amdgpu_vm_pt *parent = amdgpu_vm_pt_parent(entry);
    struct opu_mo *mo = entry->mo;
    struct opu_device *odev = params->odev;
	uint64_t pde, pt, flags;

    if (!parent)
        return 0;

    struct pmo = parent->mo;

    flags = opu_mo_pd_flags(odev, mo, level);
	amdgpu_mc_get_pde_for_mo(mo, level, &pt, &flags);
	pde = (entry - parent->entries) * 8;

    /* Handle dst addr not aligned with 4k, dst addr must 128bytes align */
    flags |= OPU_PT_BYTE_OFFSET2FLAGS(entry->mo_offset);
	r = vm->update_funcs->update(params, pmo, pde + parent->mo_offset, pt,
					1, 0, flags, false);
    return r;
}

/**
 * amdgpu_vm_alloc_pts - Allocate a specific page table
 *
 * @params:
 * @cursor: Which page table to allocate
 * @need_spe:
 *
 * Make sure a specific page table or directory is allocated.
 *
 * Returns:
 * 1 if page table needed to be allocated, 0 if page table was already
 * allocated, negative errno if an error occurred.
 */
static int amdgpu_vm_alloc_pts(struct opu_vm_update_params *params;
			       struct amdgpu_vm_pt_cursor *cursor, bool need_spe)
{
	struct amdgpu_vm_pt *entry = cursor->entry;
	struct amdgpu_mo *pt_mo;
	struct opu_device *odev = params->odev;
    struct opu_vm *vm = param->vm;
    uint32_t entry_mo_index = 0;
    uint32_t shared_mo_byte_offset = 0;

    bool useSharedMo;
    uint64_t addr;
    void *cpu_addr;
	int r = 0;

	if (entry->bo)
		return 0;

    if (((cursor->level < OPU_VM_PTE) || ((cursor->level == OPU_VM_PTE) && need_spe)) && !entry->entries) {
        uint32_t num_entries;
        uint32_t i;

        DRM_INFO((cursor->level == OPU_VM_PTE) && (!odev->vm_mgr.vm_cap.use_64_page));

        num_entries = opu_vm_num_entries(odev, cursor->level);

        entry->entries = kvmalloc_array(num_entries,
                    sizeof(*entry->entries), GFP_KERNEL | __GFP_ZERO)

        if (!entry->entries)
            return -ENOMEM;

        memset(entry->entries, 0, num_entries * sizeof(*entry->entries));
        entry->num_entries = num_entries;
        for (i = 0; i < num_entries; i++) {
            entry->entries[i].parent = entry;
            entry->entries[i].vm = vm;
        }
    }

    if (entry->mo)
        return 0;

    use_shared_mo = opu_vm_get_entry_mo_index(odev, entry, cursor->level, &entry_mo_index,
                            &shared_mo_byte_offset);

    if (!use_shared_mo || (use_shared_mo && !entry->parent->entries[entry_mo_index].shared_mo)) {
        r = opu_memobj_create_internal(odev, OPU_HEAP_FB_VISIABLE,
                            opu_vm_mo_size(odev, cursor->level),
                            PAGE_SIZE, &addr, &cpu_addr, &pt_mo);
        if (r)
            goto free_entry;

        r = opu_vm_clear_mo(odev, vm, pt_mo, cursor->level);

        if (r)
            goto free_mo;

        if (use_share_mo) {
            entry->parent->entries[entry_mo_index].shared_mo = pt_mo;
        } else {
            entry->mo = pt_mo;
            entry->mo_offset = 0;
            entry->is_shared_mo = false;
        }
    }

    if (use_shared_mo) {
        /* share mo with sibling entries,  and share mo has bee allocated */
        entry->mo = entry->parent->entries[entry_mo_index].shared_mo;
        entry->mo_offset = shared_mo_byte_offset;
        entry->is_shared_mo = true;
    }

    /* root level */
    if (!entry->parent)
        return 0;

    /* updatae this pd/pt addr to parent level*/
    r = opu_vm_update_pde(params, vm, entry, cursor->level - 1);
    if (r)
        goto free_mo;

    return 0;
free_mo:
    if (entry->mo) {
        entry->mo = NULL;
        entry->mo_offset = 0;
    }

    if (pt_mo)
        opu_memobj_free_internal(pt_mo, &cpu_addr);
free_entry:
    if (entry->entries)
        kvfree(entry->entries);
    entry->entries = NULL;
    return r;
}

/**
 * amdgpu_vm_free_pts - free PD/PT levels
 *
 * @adev: amdgpu device structure
 * @vm: amdgpu vm structure
 * @start: optional cursor where to start freeing PDs/PTs
 *
 * Free the page directory or page table level and all sub levels.
 */
static void amdgpu_vm_free_pts(struct amdgpu_device *adev,
			       struct amdgpu_vm *vm,
			       struct amdgpu_vm_pt_cursor *start)
{
	struct amdgpu_vm_pt_cursor cursor;
	struct amdgpu_vm_pt *entry;

	for_each_amdgpu_vm_pt_dfs_safe(adev, vm, start, cursor, entry)
		amdgpu_vm_free_table(entry);

	if (start)
		amdgpu_vm_free_table(start->entry);
}

/**
 * amdgpu_vm_need_pipeline_sync - Check if pipe sync is needed for job.
 *
 * @ring: ring on which the job will be submitted
 * @job: job to submit
 *
 * Returns:
 * True if sync is needed.
 */
bool amdgpu_vm_need_pipeline_sync(struct amdgpu_ring_mgr *ring,
				  struct amdgpu_job *job)
{
	struct amdgpu_device *adev = ring->odev;
	struct amdgpu_vmid_mgr *id_mgr = &odev->vm_mgr.id_mgr;
	struct amdgpu_vmid *id;
	bool vm_flush_needed = true;

	if (job->vmid == 0)
		return false;
	id = &id_mgr->ids[job->vmid];

	return vm_flush_needed;
}

uint32_t opu_vm_flush_size_in_dw(struct opu_ring_mgr *ring, struct opu_job *job)
{
    uint32_t dw = 0;
    if (likely(job->vm)) {
        dw = ring->funcs->invalidate_tlb_size * opu_setting_vm_tlb_inv_threshold +
            ring->funcs->wati_fence64_size + ring->funcs->emit_fence_size;
    }
    return ndw;
}

/**
 * amdgpu_vm_flush - hardware flush the vm
 *
 * @ring: ring to use for flush
 * @job:  related job
 * @need_pipe_sync: is pipe sync needed
 *
 * Emit a VM flush when it is necessary.
 *
 * Returns:
 * 0 on success, errno otherwise.
 */
int amdgpu_vm_flush(struct amdgpu_ring_mgr *ring, struct amdgpu_job *job,
		    bool need_pipe_sync)
{
	struct amdgpu_device *adev = ring->adev;
    uint16_t engine = ring->engine_type;
    uint16_t sync_ring_idx = ring->idx;
	struct amdgpu_vmid_mgr *id_mgr = &adev->vm_mgr.id_mgr[vmhub];
	struct amdgpu_vmid *id = &id_mgr->ids[job->vmid];

    if (likely(job->vm)) {
        bool vmid_switch_inv = !(id->inv_tlb_eng_mask & (1u << engine))
        struct opu_vm_inv_tlb_sync_info *sync_info = &job->vm->inv_tlb_sync_info[engine];

        if (!id->inv_tlb_eng_mask)
            odev->chip_funcs.set_vmid_pt_base(odev, job->vmid,
                                job->vm->root.mo->mm_node_array[0].start);

        mutex_lock(&job->vm->free_lock);
        if (sync_info->seqno > sync_info->last_wait_seqno[sync_ring_idx]) {
            ring->funcs->emit_wait_fence64(ring, sync_info->hw_sem.addr, sync_info->seqno, 0, 0)
            sync_info->last_wait_seqno[sync_ring_idx] = sync_info->seqno;
        }

        if (unlikely(vmid_switch_inv || job->vm->inv_tlb_sync_info[engine].freed_count)) {
            /* compute pipe tlb inv for mmu tlbl1_cu
             * dma pip tlb inv for mmu tlbl1_dma */
            if (unlikey(engine == OPU_ENGINE_OCN)) {
            }
            if (vmid_switch_inv || (job->vm->inv_tlb_sync_info[engine].freed_count >=
                            opu_setting_vm_tlb_inv_threshold)) {
                ring->funcs->emit_invalidate_tlb(ring, job->vmid, 0, 0);

                if (vmid_switch_inv)
                    id->inv_tlb_eng_mask != (1u << engine);
            } else {
                struct opu_bo_va *bo_va;
                list_for_each_entry(bo_va, &job->vm->freed, list) {
                    uint64_t    base_addr;
                    uint64_t    size_in_bytes;
                    if (bo_va->bo_va_mapping.inv_tlb_eng_mask & (1 << engine))
                        continue;
                    base_addr = (bo_va->bo_va_mapping.end + bo_va->bo_va_mapping.offset)
                                    << OPU_PAGE4K_SHIFT;
                    size_in_bytes = (bo_va->bo_va_mapping.end - bo_va->bo_va_mapping.start -
                                bo_va->bo_va_mapping.offset + 1) << OPU_PAGE4K_SHIFT;
                    ring->funcs->emit_invalidate_tlb(ring, job->vmid, base_addr,
                                        base_addr + size_in_bytes -1);
                }
            }
        }

        ring->funcs->emit_fence(ring, job, sync_info->hw_sem.addr, ++sync_info->seqno,
                        OPU_EMIT_FENCE_64);
        sync_info->last_wait_seqno[sync_ring_idx] = sync_info->seqno;
        sync_info->hw_sem.emitted_seq = sync_info->seqno;

        opu_vm_clear_freed_mapping(job->vm, engine);
        mutex_unlock(&job->vm->freed_lock);
    }

    if (need_pipe_sync)
        opu_ring_mgr_emit_pipeline_sync(ring);

    return r;

free_job:
    --job->vm->inv_tlb_sync_info[ring->engine_type].seqno;
    if (other_job)
        opu_job_free(other_job);
unlock_freed_lock:
    mutex_unlock(&job->vm->freed_lock);
    return r;
}

uint64_t opu_vm_get_spe_pa(const dma_addr_t *pages_addr, uint32_t pages_order, uint64_t addr)
{
    uint64_t result;
    if (pages_order) {
        result = pages_addr[addr >> OPU_PAGE64K_SHIFT];
    } else {
        result = pages_addr[addr >> OPU_PAGE4K_SHIFT];
    }
    result &= 0xFFFFFFFFF000ULL;

    return result;
}

/*
 * amdgpu_vm_update_flags - figure out flags for PTE updates
 *
 * Make sure to set the right flags for the PTEs at the desired level.
 */
static void amdgpu_vm_update_flags(struct amdgpu_vm_update_params *params,
				   struct amdgpu_mo *pt, unsigned int level,
				   uint64_t pe, uint64_t addr,
				   unsigned int count, uint32_t incr,
				   uint64_t flags)
{
	flags |= AMDGPU_PDE_FRAME_LEVEL(level);
	if (level == AMDGPU_VM_PTE && params->odev->vm_mgr.vm_caps.use_64k_page) {
		flags |= AMDGPU_PTE_PAGE_SIZE;
	}
    if (level == OPU_VM_PDE)
		flags |= OPU_PDE_AS_PTE;

	params->vm->update_funcs->update(params, mo, pe, addr, count, incr,
					 flags, true);
}

/**
 * amdgpu_vm_fragment - get fragment for PTEs
 *
 * @params: see amdgpu_vm_update_params definition
 * @start: first PTE to handle
 * @end: last PTE to handle
 * @flags: hw mapping flags
 * @frag: resulting fragment size
 * @frag_end: end of this fragment
 *
 * Returns the first possible fragment for the start and end address.
 */
static void amdgpu_vm_fragment(struct amdgpu_vm_update_params *params,
			       uint64_t start, uint64_t end, uint64_t dst, uint64_t flags,
			       unsigned int *frag, uint64_t *frag_end)
{
	/**
	 * The MC L1 TLB supports variable sized pages, based on a fragment
	 * field in the PTE. When this field is set to a non-zero value, page
	 * granularity is increased from 4KB to (1 << (12 + frag)). The PTE
	 * flags are considered valid for all PTEs within the fragment range
	 * and corresponding mappings are assumed to be physically contiguous.
	 *
	 * The L1 TLB can store a single PTE for the whole fragment,
	 * significantly increasing the space available for translation
	 * caching. This leads to large improvements in throughput when the
	 * TLB is under pressure.
	 *
	 * The L2 TLB distributes small and large fragments into two
	 * asymmetric partitions. The large fragment cache is significantly
	 * larger. Thus, we try to use large fragments wherever possible.
	 * Userspace can support this by aligning virtual base address and
	 * allocation size to the fragment size.
	 *
	 * Starting with Vega10 the fragment size only controls the L1. The L2
	 * is now directly feed with small/huge/giant pages from the walker.
	 */
	unsigned max_frag;
    uint64_t start_opu_pfn;
    uint64_t end_opu_pfn;

	max_frag = params->adev->vm_mgr.fragment_size;

	/* system pages are non continuously */
	if (!max_frag || params->pages_addr) {
		*frag = 0;
		*frag_end = end;
		return;
    }

	if (params->odev->vm_mgr.vm_caps.use_64k_page) {
        start = (start << OPU_PAGE4K_SHIFT) >> OPU_PAGE64K_SHIFT;
        end = (end << OPU_PAGE4K_SHIFT) >> OPU_PAGE64K_SHIFT;
    }

	/* This intentionally wraps around if no bit is set */
	*frag = min((unsigned)ffs(start) - 1, (unsigned)fls64(end - start) - 1);

	if (*frag >= max_frag) {
		*frag = max_frag;
    }
	*frag_end = start + (1 << *frag);

    /* frag_end in 4KB unit */
    if (params->odev->vm_mgr.vm_caps.use_64_page)
        *frag_end = ((*frag_end) << OPU_PAGE64K_SHIFT) >> OPU_PAGE4K_SHIFT;

    if (*frag < MIN_FRAGMENT_SIZE)
        *frag = 0;
}

static bool opu_vm_can_huge_page(struct opu_device *odev, uint64_t start,
                        uint64_t end, uint64_t flags)
{
    const uint64_t huge_page_count = OPU_VM_PTE_COUNT(odev) *
                        (odev->vm_mgr.vm_caps.use_64k_page? 16:1);
    const uint64_t huge_page_count_mask = huge_page_count - 1;

    return (opu_setting_vm_use_huge_page &&
            (!(flags & OPU_PTE_SYSTEM))  &&
            ((end - start) >= huge_page_count ) &&
            (!(start & huge_page_count_mask)));
}

/**
 * amdgpu_vm_update_ptes - make sure that page tables are valid
 *
 * @params: see amdgpu_vm_update_params definition
 * @start: start of GPU address range
 * @end: end of GPU address range
 * @dst: destination address to map to, the next dst inside the function
 * @flags: mapping flags
 *
 * Update the page tables in the range @start - @end.
 *
 * Returns:
 * 0 for success, -EINVAL for failure.
 */
static int amdgpu_vm_update_ptes(struct amdgpu_vm_update_params *params,
				 uint64_t start, uint64_t end,
				 uint64_t dst, uint64_t flags)
{
	struct amdgpu_device *adev = params->adev;
	struct amdgpu_vm_pt_cursor cursor;
	uint64_t frag_start = start, frag_end;
	unsigned int frag;
	int r;

    const uint64_t huge_page_size = OPU_VM_PTE_COUNT(odev) *
                (odev->vm_mgr.vm_caps.use_64k_page? 16 : 1) << PAGE_SHIFT;

    const bool need_spe = (odev->vm_mgr.vm_caps.use_64_page &&
                    params->pages_addr && (!params->page_order));

    if (odev->vm_mgr.vm_caps.use_64k_page &&
            (((start << PAGE_SHIFT) & OPU_PAGE64K_MASK) ||
             ((end << PAGE_SHIFT) & OPU_PAGE64K_MASK)))
        return -EINVAL;

	/* figure out the initial fragment */
	amdgpu_vm_fragment(params, frag_start, end, flags, &frag, &frag_end);

	/* walk over the address space and update the PTs */
	amdgpu_vm_pt_start(odev, params->vm, start, &cursor);
	while (cursor.pfn < end) {
		unsigned shift, mask;
		uint64_t incr, entry_end, pe_start;
		struct opu_mo *pt;

		/* make sure that the page tables covering the
		 * address range are actually allocated
		 */
		r = amdgpu_vm_alloc_pts(params->adev, params->vm,
						&cursor, params->immediate);
		if (r)
			return r;

        if ((cursor.level < OPU_VM_PDE) ||
                ((cursor.level == OPU_VM_PDE) &&
                 (!opu_vm_can_huge_page(odev, cursor.pfn, end, flags))) ||
                ((cursor.level == OPU_VM_PTE) && need_spe)) {
            if (!opu_vm_pt_descendant(odev, &cursor))
                return -ENOENT;
            continue;
        }

        pt = cursor.entry->mo;

		shift = amdgpu_vm_level_shift(adev, cursor.level);
		// parent_shift = amdgpu_vm_level_shift(adev, cursor.level - 1);


		/* Looks good so far, calculate parameters for the update */
		incr = (uint64_t)OPU_PAGE4K_SIZE;
        if (cursor.level == OPU_VM_PDE)
            incr = huge_page_size;
        else if (odev->vm_mgr.vm_caps.use_64k_page)
            incr = (uint64_t)OPU_PAGE64K_SIZE;

		mask = amdgpu_vm_entries_mask(adev, cursor.level);
		pe_start = ((cursor.pfn >> shift) & mask) * 8;
		entry_end = ((uint64_t)mask + 1) << shift;
		entry_end += cursor.pfn & ~(entry_end - 1);
		entry_end = min(entry_end, end);

		do {
            uint64_t upd_end = (cursor.level == OPU_VM_PDE) ?
                                (entry_end & ~((1ull << shift) - 1)) : min(entry_end, frag_end);
			unsigned nptes = (upd_end - frag_start) >> shift;
            uint64_t cur_frag = (cursor.level == OPU_VM_PTE) ? OPU_PTE_FRAG(frag) : 0;
			uint64_t upd_flags = flags | AMDGPU_PTE_FRAG(frag);

			amdgpu_vm_update_flags(params, pt, cursor.level, pe_start + cursor.entry->mo_offset,
                            dst, nptes, cur_frag ? 0 : incr, flags | cur_flags);

			pe_start += nptes * 8;
			dst += (uint64_t)nptes * incr;

			frag_start = upd_end;
			if ((frag_start >= frag_end) || (cursor.level == OPU_VM_PDE)) {
				/* figure out the next fragment */
				amdgpu_vm_fragment(params, frag_start, end, dst,
						   flags, &frag, &frag_end);
				if (cursor.level == OPU_VM_PDE)
					break;
			}
		} while (frag_start < entry_end);

		if (amdgpu_vm_pt_descendant(adev, &cursor)) {
			/* Free all child entries.
			 * Update the tables with the flags and addresses and free up subsequent
			 * tables in the case of huge pages or freed up areas.
			 * This is the maximum you can free, because all other page tables are not
			 * completely covered by the range and so potentially still in use.
			 */
			while (cursor.pfn < frag_start) {
				/* Make sure previous mapping is freed */
				amdgpu_vm_free_pts(adev, params->vm, &cursor);
				amdgpu_vm_pt_next(adev, &cursor);
			}
		} else {
			/* or just move on to the next on the same level. */
			amdgpu_vm_pt_next(adev, &cursor);
		}
	}

	return 0;
}

/**
 * amdgpu_vm_bo_update_mapping - update a mapping in the vm page table
 *
 * @adev: amdgpu_device pointer of the VM
 * @bo_adev: amdgpu_device pointer of the mapped BO
 * @vm: requested vm
 * @immediate: immediate submission in a page fault
 * @unlocked: unlocked invalidation during MM callback
 * @resv: fences we need to sync to
 * @start: start of mapped range
 * @last: last mapped entry
 * @flags: flags for the entries
 * @offset: offset into nodes and pages_addr
 * @res: ttm_resource to map
 * @pages_addr: DMA addresses to use for mapping
 * @fence: optional resulting fence
 * @table_freed: return true if page table is freed
 *
 * Fill in the page table entries between @start and @last.
 *
 * Returns:
 * 0 for success, -EINVAL for failure.
 */
int amdgpu_vm_bo_update_mapping(struct amdgpu_device *adev,
				struct amdgpu_vm *vm, struct dma_fence *exclusive,
				uint64_t start, uint64_t last,
				uint64_t flags, uint64_t addr,
				dma_addr_t *pages_addr,
				uint32_t page_order,
				struct dma_fence **fence)
{
	struct amdgpu_vm_update_params params;
	int r;

	memset(&params, 0, sizeof(params));
	params.adev = adev;
	params.vm = vm;
	params.pages_addr = pages_addr;
	params.page_order = page_order;

	r = vm->update_funcs->prepare(&params, exclusive);
	if (r)
        return r;

	r = amdgpu_vm_update_ptes(&params, start, last + 1, addr, flags);
	if (r)
		goto error_unlock;

	r = vm->update_funcs->commit(&params, fence);
	if (r)
		goto error_unlock;


error_unlock:
	vm->update_funcs->cleanup(&params);
	return r;
}

static int opu_vm_bo_split_mapping(struct opu_device *odev,
                        struct dma_fence *exclusive,
                        dma_addr_t  *pages_addr,
                        uint32_t    page_order,
                        struct  opu_vm *vm;
                        struct opu_bo_va_mapping *mapping,
                        uint64_t flags,
                        struct opu_device *mo_odev,
                        struct opu_memobj *mo,
                        struct dma_fence **fence)
{
    struct dum_mm_node *nodes = mo ? mo->mm_node_array : NULL;
    uint64_t vram_base_offset = mo_odev->vm_mgr.vram_base_offset;
    uint64_t pfn, start = mapping->start + (mapping->offset >> PAGE_SHIFT);
    int r;

    pfn = mapping->offset >> PAGE_SHIFT;

    if (nodes) {
        while(pfn >= (nodes->size >> PAGE_SHIFT)) {
            pfn -= (nodes->size >> PAGE_SHIFT);
            ++nodes;
        }
    }

    do {
        dma_addr_t *dma_addr = NULL;
        uint64_t max_entries;
        uint64_t addr, last;

        if (nodes) {
            addr = nodes->start;
            max_entries = ((nodes->size >> PAGE_SHIFT) - pfn);

            if (!mo->is_dev_mem) {
                addr = pfn << PAGE_SHIFT;
                dma_addr = pages_addr;
            } else {
                addr += vram_base_offset;
                addr += pfn << PAGE_SHIFT;
            }
        } else {
            addr = 0;
            max_entries = S64_MAX;
        }

        last = min((uint64_t)mapping->end, start + max_entries - 1);
        r = opu_vm_bo_update_mapping(odev, vm, exclusive,
                            start, last, flags, addr,
                            dma_addr, page_order, fence);
        if (r)
            return r;

        pfn += (last - start + 1);
        if (nodes && (nodes->size >> PAGE_SHIFT) == pfn) {
            pfn = 0;
            ++nodes;
        }
        start = last + 1;
    } while(unlikely(start != (mapping->end + 1)));

    return 0;
}

int opu_vm_bo_update(struct opu_device *odev, struct opu_bo_va *bo_va,
                struct dma_fence *exclusive, bool clear)
{
    struct opu_bo *bo = bo_va->bo;
    struct opu_vm *vm = bo_Va->vm;
    struct opu_bo_va_mapping *mapping = &bo_va->bo_va_mapping;
    struct opu_mo *mo = bo->mo;
    dam_addr_t *pages_addr = NULL;
    uint32_t page_order = 0;
    struct drm_mm_node *nodes;
    struct dma_fence *old;
    struct dma_fence *last_update = NULL;
    struct dma_Fence __rcu **ptr;
    uint64_t flags;
    struct opu_device *mo_odev = (mo && mo->is_valid) ? mo->odev : odev;
    int r;

    if (!odev->vm_mgr.vm_caps.support_vm_pt)
        return 0;

    if (mo && mo->is_valid && !mo->is_dev_mem && !mo->dma_addr)
        return -EINVAL;

    pages_addr = mo ? mo->dma_addr :0;
    page_order = mo ? mo->pages.order :0;

    if (bo) {
        flags = opu_mo_pte_flags(odev, mo, mapping->flags);
        if (bo->is_unified) {
            flags &= ~(OPU_PTE_RESIDENT);
        }
    } else {
        flags = 0x0;
    }

    if (clear) {
        nodes = NULL;
        flags &= ~(OPU_PTE_VALID);
    }

    mutex_lock(&vm->lock);
    r = opu_vm_bo_split_mapping(odev, exclusive, pages_addr, page_order,
                        vm, mapping, flags, mo_odev,
                        mo, &last_update);
    ptr = &vm->last_update;

    old = rcu_dereference_protected(*ptr, 1);
    rcu_assign_pointer(*ptr, last_update);
    mutex_unlock(&vm->lock);
    dma_fence_put(old);

    if (r)
        return r;

    bo_va->cleared = clear;
    return 0;
}
/**
 * amdgpu_vm_bo_insert_map - insert a new mapping
 *
 * @adev: amdgpu_device pointer
 * @bo_va: bo_va to store the address
 * @mapping: the mapping to insert
 *
 * Insert a new mapping into all structures.
 */
static void amdgpu_vm_bo_insert_map(struct amdgpu_device *adev,
				    struct amdgpu_bo_va *bo_va,
				    struct amdgpu_bo_va_mapping *mapping)
{
	amdgpu_vm_it_insert(mapping, &bo_va->vm->va);
}

/**
 * amdgpu_vm_bo_map - map bo inside a vm
 *
 * @adev: amdgpu_device pointer
 * @bo_va: bo_va to store the address
 * @saddr: where to map the BO
 * @offset: requested offset in the BO
 * @size: BO size in bytes
 * @flags: attributes of pages (read/write/valid/etc.)
 *
 * Add a mapping of the BO at the specefied addr into the VM.
 *
 * Returns:
 * 0 for success, error for failure.
 *
 * Object has to be reserved and unreserved outside!
 */
int amdgpu_vm_bo_map(struct amdgpu_device *adev,
		     struct amdgpu_bo_va *bo_va,
		     uint64_t saddr, uint64_t offset,
		     uint64_t size, uint64_t flags)
{
	struct amdgpu_bo_va_mapping *mapping, *tmp;
	struct amdgpu_bo *bo = bo_va->bo;
	struct amdgpu_vm *vm = bo_va->vm;
	uint64_t eaddr;

	/* validate the parameters */
	if (saddr & OPU_PAGE4K_MASK || offset & OPU_PAGE4K_MASK ||
	    size == 0 || size & OPU_PAGE4K_MASK)
		return -EINVAL;

	/* make sure object fit at this offset */
	eaddr = saddr + size - 1;
	if (saddr >= eaddr ||
	    (bo && offset + size > bo->size))
		return -EINVAL;

	saddr /= OPU_PAGE4K_SIZE;
	eaddr /= OPU_PAGE4K_SIZE;

	tmp = amdgpu_vm_it_iter_first(&vm->va, saddr, eaddr);
	if (tmp) {
		/* bo and tmp overlap, invalid addr */
		dev_err(adev->dev, "bo %p va 0x%010Lx-0x%010Lx conflict with "
			"0x%010Lx-0x%010Lx\n", bo, saddr, eaddr,
			tmp->start, tmp->last + 1);
		return -EINVAL;
	}

	mapping = &bo_va->bo_va_mapping;
	mapping->start = saddr;
	mapping->last = eaddr;
	mapping->offset = offset;
	mapping->flags = flags;

	amdgpu_vm_bo_insert_map(adev, bo_va, mapping);

	return 0;
}

/**
 * amdgpu_vm_bo_unmap - remove bo mapping from vm
 *
 * @adev: amdgpu_device pointer
 * @bo_va: bo_va to remove the address from
 * @saddr: where to the BO is mapped
 *
 * Remove a mapping of the BO at the specefied addr from the VM.
 *
 * Returns:
 * 0 for success, error for failure.
 *
 * Object has to be reserved and unreserved outside!
 */
int amdgpu_vm_bo_unmap(struct amdgpu_device *adev,
		       struct amdgpu_bo_va *bo_va,
		       uint64_t saddr)
{
	struct amdgpu_bo_va_mapping *mapping = &bo_va->bo_va_mapping;
	struct amdgpu_vm *vm = bo_va->vm;
	amdgpu_vm_it_remove(mapping, &vm->va);

	return 0;
}

/**
 * amdgpu_vm_bo_lookup_mapping - find mapping by address
 *
 * @vm: the requested VM
 * @addr: the address
 *
 * Find a mapping by it's address.
 *
 * Returns:
 * The amdgpu_bo_va_mapping matching for addr or NULL
 *
 */
struct amdgpu_bo_va_mapping *amdgpu_vm_bo_lookup_mapping(struct amdgpu_vm *vm,
							 uint64_t addr)
{
	return amdgpu_vm_it_iter_first(&vm->va, addr, addr);
}

/**
 * amdgpu_vm_bo_lookup_mapping - find mapping by address
 *
 * @vm: the requested VM
 * @addr: the address
 *
 * Find a mapping by it's address.
 *
 * Returns:
 * The amdgpu_bo_va_mapping matching for addr or NULL
 *
 */
struct amdgpu_bo_va_mapping *amdgpu_vm_bo_lookup_mapping(struct amdgpu_vm *vm,
							 uint64_t engine)
{
    struct opu_bo_va *bo_va, *tmp;
    lockdep_assert_held(&vm->freed_lock);
    list_for_each_entry_safe(bo_va, tmp, &vm->freed, list) {
        if (!(bo_va->bo_va_mapping.inv_tlb_eng_mask & (1u << engine))) {
            bo_va->bo_va_mapping.inv_tlb_eng_mask |= 1u << engine;
            vm->inv_tlb_sync_info[engine].freed_count--;
        }
        if (bo_va->bo_va_mapping.inv_tlb_eng_mask == vm->full_eng_mask) {
            list_del(&bo_va->list);
            kvfree(bo_va);
            vm->freed_count--;
        }
    }
}

/**
 * amdgpu_vm_wait_idle - wait for the VM to become idle
 *
 * @vm: VM object to wait for
 * @timeout: timeout to wait for VM to become idle
 */
long amdgpu_vm_wait_idle(struct amdgpu_vm *vm, long timeout)
{
	timeout = dma_resv_wait_timeout(vm->root.bo->tbo.base.resv, true,
					true, timeout);
	if (timeout <= 0)
		return timeout;

	return dma_fence_wait_timeout(vm->last_unlocked, true, timeout);
}

int opu_vm_bo_check_align(struct opu_device *odev, struct opu_bo *obo,
                    uint32_t align, uint64_t size)
{
    uint32_t valid_align = odev->vm_mgr.vm_caps.support_vm_pt ?
                    odev->vm_mgr.va_align :  OPU_PAGE4K_SIZE;
    return (IS_ALIGNED(align, valid_align) && IS_ALIGNED(size, valid_align)) ? 0 : -EINVAL;
}

int opu_update_dev_page_table(struct opu_bo *obo, uint64_t va, struct opu_mo *mo, bool clear)
{
    int err  = 0;
    struct opu_bo_va *bo_va = NULL;
    struct opu_vm *vm = NULL;
    uint64_t va_start = va >> PAGE_SHIFT;
    uint64_t va_end = 0;
    uint64_t pte_flags = 0;
    uint64_t addr = 0;
    bool need_invalid_tlb = false;
    struct dma_fence *exclusive, **last_update;
    struct opu_bo_va_mapping *mapping = NULL;
    struct opu_mo *tmp_mo = kzalloc(sizeof(*mo), GFP_KERNEL);
    if (!tmp_mo)
        return -ENOMEM;
    memcpy(tmp_mo, mo, sizeof(*mo));

    if (!tmp_mo->is_dev_mem && tmp_mo->pages.order) {
        const uint64_t npages = tmp_mo->pages.npages;
        uint32_t order = tmp_mo->pages.order;
        uint64_t i = 0;
        tmp_mo->pages.order = 0;
        tmp_mo->dma_addr = kvzalloc(npages * sizeof(dma_addr_t), GFP_KERNEL);
        for (i = 0; i < npages; i++) {
            tmp_mo->dma_addr[i] = mo->dma_addr[i >> order] + (i % (1 << order)) * PAGE_SIZE;
        }
    }
    if (!obo->is_unified)
        return -EINVAL;
    // unified mem not support multi proces , just one bo_va for now
    list_for_each_entry(bo_va, &obo->bo_va, list) {
        break;
    }

    if (&bo_va->list == &obo->bo_va) {
        DRM_ERROR("not find bo_va when update_dev_page_table\n");
        return -EINVAL;
    }

    mapping = &bo_va->bo_va_mapping;
    if (mapping->offset != 0) {
        DRM_ERROR("mapping->offset != 0 when udpate_dev_page_table\n");
        return -EINVAL;
    }
    if (!tmp_mo->is_dev_mem && !tmp_mo->dma_addr) {
        DRM_ERROR("mapping->offset != 0 when udpate_dev_page_table\n");
        return -EINVAL;
    }
    va_end = va_start + (tmp_mo->size >> PAGE_SHIFT) - 1;
    vm = bo_va->vm;

    DRM_INFO("update_dev(dev_id=%d)page_table va start=0x%llx, va end=%x%llx, clear=%d", 
                obo->odev->opuid, va_start, va_end, clear);

    pte_flags = opu_mo_pte_flags(obo->odev, tmp_mo, mapping->flags);
    if (clear) {
        if (va_start <= mapping->end && va_end >= mapping->start) {
            pte_flags &= ~(OPU_PTE_RESIDENT);
            need_invalid_tlb = true;
        } else {
            pte_flags &= ~(OPU_PTE_VALID);
            need_invalid_tlb = true;
        }
    }

    if (!(mapping->flags & OPU_PTE_READABLE))
        pte_flags &= ~OPU_PTE_READABLE;

    if (!(mapping->flags & OPU_PTE_WRITEABLE))
        pte_flags &= ~OPU_PTE_WRITEABLE;

    if (tmp_mo->is_dev_mem) {
        addr = tmp_mo->mm_node_array[0].start;
    } else {
        addr = 0;
    }
    if (clear)
        exclusive = NULL;

    last_update = &vm->last_update;
    err = opu_vm_bo_update_mapping(obo->odev, vm, exclusive, va_start, va_end,
                    pte_flags, addr, tmp_mo->dma_addr, tmp_mo->pages.order, last_update);

    if (err)
        DRM_ERROR("vm_bo_update_mapping failed when update_dev_page_table\n");

    if (need_invalid_tlb) {
        // TODO
    }

    if (!mo->is_dev_mem && mo->pages.order) {
        kvfree(tmp_mo->dma_addr);
    }
    kvfree(tmp_mo);
    return err;
}

/* remove pst when swithc to other process and vm destroy */
int opu_vm_remote_pst_buffers(struct opu_device *odev, struct opu_vm *vm, bool clear)
{
    struct opu_pst_buffer *pst_buffer=NULL, *tmp=NULL;
    int err = 0;
    mutex_lock(&vm->pst_lock);
    list_for_each_entry_safe(pst_buffer, tmp, &vm->pst_head, list) {
        err = opu_submit_remove_pst_buffer(odev, vm, pst_buffer, false);
        if (err)
            DRM_ERROR("submit remove pst_buffer faile\n");
        else {
            if (clear) {
                opu_release_pst_buffers(&pst_buffer, 1);
                list_del(&pst_buffer->list);
            }
        }
    }
    mutex_unlock(&vm->pst_lock);
    return err;
}

int opu_vm_readd_pst_buffer(struct opu_device *odev, struct opu_vm *vm )
{
    struct opu_pst_buffer *pst_buffer;
    let err = 0;
    mutex_lock(&vm->pst_lock);
    list_for_each_entry(pst_buffer, &vm->pst_head, list) {
        ret = opu_submit_add_pst_buffer(odev, vm, pst_buffer);
        if (ret)
            DRM_ERROR("readd pst buffer failed\n");
    }
    mutex_unlock(&vm->pst_lock);
    return err;
}

/**
 * amdgpu_vm_init - initialize a vm instance
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 * @pasid: Process address space identifier
 *
 * Init @vm fields.
 *
 * Returns:
 * 0 for success, error for failure.
 */
int opu_vm_init(struct opu_device *odev, struct opu_vm *vm, uint32_t pasid) {
    int r;
    uint32_t i, j;
    struct opu_vm_pt_cursor cursor;
    struct opu_vm_update_params params;

    if (odev->vm_mgr.vm_caps.support_vm_pt)
        return 0;

    atomic_set(&vm->num_queues, 0);
    vm->root.vm = vm;
    opu_vm_pt_start(odev, vm, 0, &cursor);

    vm->va = RB_ROOT_CACHED;
    vm->reserved_vmid = NULL;

    r = drm_sched_entity_init(&vm->paging_entity, DRM_SCHED_PRIORITY_NORMAL,
                &odev->vm_mgr.vm_pte_scheds,
                odev->vm_mgr.vm_pte_num_scheds, NULL);
    if (r)
        return r;

    vm->use_cpu_for_update = opu_setting_vm_use_cpu_for_update;
    DRM_INFO("VM update mode is %s\n", vm->use_cpu_for_update ? "CPU" : "DMA");

    if (vm->use_cpu_for_update)
        vm->update_funcs = &opu_vm_cpu_funcs;
    else
        vm->update_funcs = &opu_vm_dma_funcs;
    vm->last_update = NULL;

    mutex_init(&vm->pst_lock);
    INIT_LIST_HEAD(&vm->pst_head);

    memset(&params, 0, sizeof(params));
    params.odev = odev;
    params.vm = vm;

    /* alloc the root table */
    r = opu_vm_alloc_pts(&params, &cursor, false);
    if (r)
        goto entity_destroy;

    mutex_init(&vm->lock);
    mutex_init(&vm->job_deps_lock);
    opu_deps_create(&vm->job_deps);
    vm->fence_context = dma_fence_context_alloc(1);
    vm->seqno = 0;

    INIT_LIST_HEAD(&vm->freed);
    mutex_init(&vm->freed_lock);
    vm->freed_count = 0;
    // vm->full_en_mask = // (1u << OPU_EN)
    memset(vm->inv_tlb_sync_info, 0, sizeof(vm->inv_tlb_sync_info));
    for (i = 0; i < ARRAY_SIZE(vm->inv_tlb_sync_info); i++) {
        r = opu_dev_qw_pool_alloc(odev, &(vm->inv_tlb_sync_info[i].hw_sem.dw_pool_offset));
        if (r)
            goto clean_up_deps_and_hw_sem;

        vm->inv_tlb_sync_info[i].hw_sem.cpu_addr = QW_POOL_OFF2CPUADDR(odev,
                    vm->inv_tlb_sync_info[i].hw_sem.dw_pool_offset);
        vm->inv_tlb_sync_info[i].hw_sem.addr = QW_POOL_OFF2ADDR(odev,
                    vm->inv_tlb_sync_info[i].hw_sem.dw_pool_offset);
    }

    return 0;
clean_up_deps_and_hw_sem:
    for (j = 0; j < i; j++) {
        opu_dev_qw_pool_free(odev, vm->inv_tlb_sync_info[i].hw_sem.dw_pool_offset);
    }
    mutex_destroy(&vm->freed_lock);
    opu_deps_free(&vm->job_deps);
entiry_destroy:
    mutex_destroy(&vm->lock);
    drm_sched_entity_destroy(&vm->paging_entity);
}

/**
 * amdgpu_vm_fini - tear down a vm instance
 *
 * @adev: amdgpu_device pointer
 * @vm: requested vm
 *
 * Tear down @vm.
 * Unbind the VM and remove all bos from the vm bo list
 */
void amdgpu_vm_fini(struct amdgpu_device *adev, struct amdgpu_vm *vm)
{
	struct amdgpu_bo_va_mapping *mapping, *tmp;
	bool prt_fini_needed = !!adev->gmc.gmc_funcs->set_prt;
	struct amdgpu_bo *root;
	int i;

	amdgpu_amdkfd_gpuvm_destroy_cb(adev, vm);

	root = amdgpu_bo_ref(vm->root.bo);
	amdgpu_bo_reserve(root, true);
	if (vm->pasid) {
		unsigned long flags;

		spin_lock_irqsave(&adev->vm_mgr.pasid_lock, flags);
		idr_remove(&adev->vm_mgr.pasid_idr, vm->pasid);
		spin_unlock_irqrestore(&adev->vm_mgr.pasid_lock, flags);
		vm->pasid = 0;
	}

	dma_fence_wait(vm->last_unlocked, false);
	dma_fence_put(vm->last_unlocked);

	list_for_each_entry_safe(mapping, tmp, &vm->freed, list) {
		if (mapping->flags & AMDGPU_PTE_PRT && prt_fini_needed) {
			amdgpu_vm_prt_fini(adev, vm);
			prt_fini_needed = false;
		}

		list_del(&mapping->list);
		amdgpu_vm_free_mapping(adev, vm, mapping, NULL);
	}

	amdgpu_vm_free_pts(adev, vm, NULL);
	amdgpu_bo_unreserve(root);
	amdgpu_bo_unref(&root);
	WARN_ON(vm->root.bo);

	drm_sched_entity_destroy(&vm->immediate);
	drm_sched_entity_destroy(&vm->delayed);

	if (!RB_EMPTY_ROOT(&vm->va.rb_root)) {
		dev_err(adev->dev, "still active bo inside vm\n");
	}
	rbtree_postorder_for_each_entry_safe(mapping, tmp,
					     &vm->va.rb_root, rb) {
		/* Don't remove the mapping here, we don't want to trigger a
		 * rebalance and the tree is about to be destroyed anyway.
		 */
		list_del(&mapping->list);
		kfree(mapping);
	}

	dma_fence_put(vm->last_update);
	for (i = 0; i < AMDGPU_MAX_VMHUBS; i++)
		amdgpu_vmid_free_reserved(adev, vm, i);
}

/**
 * amdgpu_vm_mgr_init - init the VM manager
 *
 * @adev: amdgpu_device pointer
 *
 * Initialize the VM manager structures
 */
void amdgpu_vm_mgr_init(struct amdgpu_device *adev)
{
	unsigned i;

	/* Concurrent flushes are only possible starting with Vega10 and
	 * are broken on Navi10 and Navi14.
	 */
	adev->vm_mgr.concurrent_flush = !(adev->asic_type < CHIP_VEGA10 ||
					      adev->asic_type == CHIP_NAVI10 ||
					      adev->asic_type == CHIP_NAVI14);
	amdgpu_vmid_mgr_init(adev);

	adev->vm_mgr.fence_context =
		dma_fence_context_alloc(AMDGPU_MAX_RINGS);
	for (i = 0; i < AMDGPU_MAX_RINGS; ++i)
		adev->vm_mgr.seqno[i] = 0;

	spin_lock_init(&adev->vm_mgr.prt_lock);
	atomic_set(&adev->vm_mgr.num_prt_users, 0);

	/* If not overridden by the user, by default, only in large BAR systems
	 * Compute VM tables will be updated by CPU
	 */
#ifdef CONFIG_X86_64
	if (amdgpu_vm_update_mode == -1) {
		if (amdgpu_gmc_vram_full_visible(&adev->gmc))
			adev->vm_mgr.vm_update_mode =
				AMDGPU_VM_USE_CPU_FOR_COMPUTE;
		else
			adev->vm_mgr.vm_update_mode = 0;
	} else
		adev->vm_mgr.vm_update_mode = amdgpu_vm_update_mode;
#else
	adev->vm_mgr.vm_update_mode = 0;
#endif

	idr_init(&adev->vm_mgr.pasid_idr);
	spin_lock_init(&adev->vm_mgr.pasid_lock);
}

/**
 * amdgpu_vm_mgr_fini - cleanup VM manager
 *
 * @adev: amdgpu_device pointer
 *
 * Cleanup the VM manager and free resources.
 */
void amdgpu_vm_mgr_fini(struct amdgpu_device *adev)
{
	WARN_ON(!idr_is_empty(&adev->vm_mgr.pasid_idr));
	idr_destroy(&adev->vm_mgr.pasid_idr);

	amdgpu_vmid_mgr_fini(adev);
}

