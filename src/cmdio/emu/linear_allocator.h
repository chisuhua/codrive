#pragma once
#include <list>

#define ALIGNMENT 4096
class LinearAllocator {
    public:

    LinearAllocator()
        : m_addr()
        , m_size()
        , m_min_alignment(0)
        , m_num_allocations(0)
        , m_chunk_list(nullptr) {
        }

    ~LinearAllocator() {}

    bool Init(uint64_t addr, uint64_t size, uint32_t min_alignment = ALIGNMENT) {
        m_addr = addr;
        m_size = size;
        m_min_alignment = min_alignment;

        m_chunk_list = new ChunkList();

        if (m_chunk_list) {
        }
    }

}

