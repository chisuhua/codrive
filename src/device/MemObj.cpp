#include "MemObj.h"
#include "libhsakmt.h"

// Inserts node into freelist after place.
// Assumes node will not be an end of the list (list has guard nodes).
void Allocator::insertafter(Allocator::iterator_t place, Allocator::iterator_t node) {
    assert(place->first < node->first && "Order violation");
    assert(isfree(place->second) && "Freelist operation error.");
    iterator_t next = place->second.next;
    node->second.next = next;
    node->second.prior = place;
    place->second.next = node;
    next->second.prior = node;
}

// Removes node from freelist.
// Assumes node will not be an end of the list (list has guard nodes).
void Allocator::remove(Allocator::iterator_t node) {
    assert(isfree(node->second) && "Freelist operation error.");
    node->second.prior->second.next = node->second.next;
    node->second.next->second.prior = node->second.prior;
    setused(node->second);
}

// Returns high if merge failed or the merged node.
Allocator::memory_t::iterator Allocator::merge(Allocator::memory_t::iterator low,
                                                 Allocator::memory_t::iterator high) {
    assert(isfree(low->second) && "Merge with allocated block");
    assert(isfree(high->second) && "Merge with allocated block");

    if ((char*)low->first + low->second.len != (char*)high->first) return high;

    assert(!islastfree(high->second) && "Illegal merge.");

    low->second.len += high->second.len;
    low->second.next = high->second.next;
    high->second.next->second.prior = low;

    memory.erase(high);
    return low;
}

bool Allocator::free(void* ptr, size_t bytes) {
    if (ptr == nullptr) false;

    auto iterator = memory.find(ptr);

    // Check for illegal free
    if (iterator == memory.end()) {
        assert(false && "Illegal free.");
        return false;
    }

    bool is_free = isfree(iterator->second);

    assert(!is_free && "it is already free");

    // Split node if ptr is inside the node
    if (iterator->first < ptr) {
        size_t low_node_len = VOID_PTRS_SUB(ptr, iterator->first);
        size_t high_node_len = iterator->second.len - low_node_len;
        Node& node = memory[ptr];
        iterator->second.len = low_node_len;
        node.len = high_node_len;
        insertafter(iterator, memory.find(ptr));
        iterator = memory.find(ptr);
    }

	/* check if block is whole region or part of it */
    if (iterator->second.len == bytes) {
        // Return memory to total and link node into free list
        total_free += iterator->second.len;
	} else if (iterator->second.len > bytes) {
        // Split node
        void* remaining = (char*)iterator->first + bytes;
        Node& node = memory[remaining];
        node.len = iterator->second.len - bytes;
        iterator->second.len = bytes;
        insertafter(iterator, memory.find(remaining));
	} else if (iterator->second.len > bytes) {
        assert(false && "not support to free across node");
    }

    // Could also traverse the free list which might be faster in some cases.
    auto before = iterator;
    before--;
    while (!isfree(before->second)) before--;
    assert(before->second.next->first > iterator->first && "Inconsistency in small heap.");
    insertafter(before, iterator);

    // Attempt compaction
    iterator = merge(before, iterator);
    merge(iterator, iterator->second.next);

    // Update lowHighBondary
    high.erase(ptr);
    return true;
}

void* Allocator::alloc(void *address, size_t bytes, bool fix_address) {
    // Is enough memory available?
    if ((bytes > total_free) || (bytes == 0)) return nullptr;

    iterator_t current;

    if (fix_address && address > base()) {
        bool found = false;
        // current = memory.find(address);
        for (auto itr = memory.begin(); itr != memory.end(); itr++) {
            if ((itr->first <= address) && ((itr->first + itr->second.len) > address)) {
                current = itr;
                found = true;
                break;
            }
        }
        if (!found) 
            return nullptr;
        else if (!isfree(current->second)) {
            return nullptr;
        }
    } else {
        // Walk the free list and allocate at first fitting location
        current = firstfree();
    }

    while (!islastfree(current->second)) {
      if (bytes <= current->second.len) {
        // Decrement from total
        total_free -= bytes;

        // Split node
        if (bytes != current->second.len) {
          void* remaining = (char*)current->first + bytes;
          Node& node = memory[remaining];
          node.len = current->second.len - bytes;
          current->second.len = bytes;
          iterator_t new_node = memory.find(remaining);
          insertafter(current, new_node);
        }

        remove(current);
        return current->first;
      }
      current = current->second.next;
    }
    assert(current->second.len == 0 && "Freelist corruption.");

    // Can't service the request due to fragmentation
    return nullptr;
}

void* Allocator::alloc_high(size_t bytes) {
    // Is enough memory available?
    if ((bytes > total_free) || (bytes == 0)) return nullptr;

    iterator_t current;

    // Walk the free list and allocate at first fitting location
    current = lastfree();
    while (!isfirstfree(current->second)) {
      if (bytes <= current->second.len) {
        // Decrement from total
        total_free -= bytes;

        void* alloc;
        // Split node
        if (bytes != current->second.len) {
          alloc = (char*)current->first + current->second.len - bytes;
          current->second.len -= bytes;
          Node& node = memory[alloc];
          node.len = bytes;
          setused(node);
        } else {
          alloc = current->first;
          remove(current);
        }

        high.insert(alloc);
        return alloc;
      }
      current = current->second.prior;
    }
    assert(current->second.len == 0 && "Freelist corruption.");

    // Can't service the request due to fragmentation
    return nullptr;
}

bool id_in_array(uint32_t id, uint32_t *ids_array,
		uint32_t ids_array_size)
{
	uint32_t i;

	for (i = 0; i < ids_array_size; i++) {
		if (id == ids_array[i])
			return true;
	}
	return false;
}


/* Helper function to remove ids_array from
 * obj->mapped_device_id_array
 */
void MemObj::remove_device_ids_from_mapped_array(
		uint32_t *ids_array, uint32_t ids_array_size)
{
	uint32_t i = 0, j = 0;

	if (mapped_device_id_array != ids_array) {
	    for (i = 0; i < mapped_device_id_array_size; i++) {
		    if (!id_in_array(mapped_device_id_array[i],
					ids_array, ids_array_size))
			    mapped_device_id_array[j++] = mapped_device_id_array[i];
	    }
    }
	mapped_device_id_array_size = j;
	if (!j) {
		if (mapped_device_id_array)
			free(mapped_device_id_array);
		mapped_device_id_array = nullptr;
	}
}

/* Helper function to add ids_array to
 * obj->mapped_device_id_array
 */
void MemObj::add_device_ids_to_mapped_array(
		uint32_t *ids_array, uint32_t ids_array_size)
{
	uint32_t new_array_size;

	/* Remove any potential duplicated ids */
	remove_device_ids_from_mapped_array(ids_array, ids_array_size);
	new_array_size = mapped_device_id_array_size + ids_array_size;

	mapped_device_id_array = (uint32_t *)realloc(
			mapped_device_id_array, new_array_size * sizeof(uint32_t));

	memcpy(&mapped_device_id_array[mapped_device_id_array_size],
			ids_array, ids_array_size*sizeof(uint32_t));

	mapped_device_id_array_size = new_array_size;
}

