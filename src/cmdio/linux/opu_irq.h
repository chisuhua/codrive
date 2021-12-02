#ifndef __OPU_IRQ_H__
#define __OPU_IRQ_H__

#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/irqreturn.h>
#include <drm/drm_device.h>

#define IH_DEBUG

#define MAX_SRC_DATA_LEN    4
#define IH_ENTRY_SIZE       32
#define IH_RING_SIZE        (IH_ENTRY_SIZE * 1024)
#define OPU_DEFAULT_IH_IRQ_VECS 1

union DBG_WRAP_CTRL_CMD {
};

#define DBG_WRAP_CTRL_CMD_ADDR 0x20000
enum GDB_CTRL_CMD {
    GDB_CTRL_CMD_RESUME = 0,
    GDB_CTRL_CMD_HALT = 1,
    GDB_CTRL_CMD_TRAP = 2,
    GDB_CTRL_CMD_KILL = 3,
};

struct opu_device;

extern const struct opu_ip_funcs ip_irq_funcs;

/* fenc timeout 10ms */
#define OPU_FENCE_JIFFIES_TIMEOUT   (msecs_to_jiffies(10))

#define IH_CLIENT_ID_MASK       0xffff0000
#define IH_CLIENT_ID_SHIFT      16

/* irq type */
enum opu_irq_type {
    OPU_PAGE_FAULT_IRQ = 0,
    OPU_PCI_EN_IRQ,
    OPU_THMD_INTR_HIGH,
    OPU_THMD_INTR_LOW,
    OPU_CORE_HANG_IRQ,
    OPU_CORE_IRQ,
    OPU_DMA_IRQ,
    OPU_OCN_IRQ,
    OPU_GDB_IRQ,
    OPU_IH_IRQ,
    OPU_IRQ_SOURCE_MAX,
    OPU_INVALID_IRQ = 255
};

/* irq data */
struct opu_irq_info {
    unsigned int type;
    void *handle;
    bool need_wq;  /* workqueue */
    char *wq_name;

    unsigned long timeout;
    /*funcs*/
    bool (*checker)(void *handle);
    irqreturn_t (*handler)(int irq, void *handle);
};

/* irq source */
struct opu_irq_source {
    bool enabled;
    unsigned int type;
    int irq;
    struct workqueue_struct *wq;
    struct work_struct work;
    void *handle;
    struct timer_list timer;
    unsigned long timeout;
    bool (*checker)(void *handle);
    irqreturn_t (*handler)(int irq, void *handle);
};

/* ih ring */
struct opu_ih_ring {
    struct opu_simple_ring *ring;
    struct opu_irq_souce *sources[CLIENT_MAX];
    volatile uint32_t *wptr_wb;
    bool enabled;
    struct opu_simple_ring *paging_ring;
    struct mutex paging_ring_lock;
};

struct opu_ih_payload {
    struct opu_device *odev;
    IH_ENTRY_COMMON entry;
};

/* irq mgr */
struct opu_irq_mgr {
    bool init;
    bool reused;
    /* irq */
    int irq_base;
    int total_vecs;
    int used_vecs;
    /* irq source */
    struct opu_irq_source *source[OPU_IRQ_SOURCE_MAX];
    int src_enabled;
    int src_registered;
    /* IH */
    struct opu_ih_ring *ih;
};

bool opu_irq_enabled(struct opu_device *odev, unsigned int type);
bool opu_register_irq(struct opu_device *odev, struct drm_device *ddev,
                        unsigned int type);
bool opu_unregister_irq(struct opu_device *odev, struct drm_device *ddev,
                        unsigned int type);

bool opu_enable_irq(struct opu_device *odev, unsigned int type);
bool opu_disable_irq(struct opu_device *odev, unsigned int type);

bool opu_enable_all_irq(struct opu_device *odev);
bool opu_disable_all_irq(struct opu_device *odev);

#endif
