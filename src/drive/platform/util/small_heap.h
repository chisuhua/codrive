// A simple first fit memory allocator with eager compaction.  For use with few
// items (where list iteration is faster than trees).
// Not thread safe!

#ifndef HSA_RUNTME_CORE_UTIL_SMALL_HEAP_H_
#define HSA_RUNTME_CORE_UTIL_SMALL_HEAP_H_

#include "utils.h"

#include <map>
#include <set>

class SmallHeap {
 private:
  struct Node;
  typedef std::map<void*, Node> memory_t;
  typedef memory_t::iterator iterator_t;

  struct Node {
    size_t len;
    iterator_t next;
    iterator_t prior;
  };

  SmallHeap(const SmallHeap& rhs) = delete;
  SmallHeap& operator=(const SmallHeap& rhs) = delete;

  void* const pool;
  const size_t length;

  size_t total_free;
  memory_t memory;
  std::set<void*> high;

  __forceinline bool isfree(const Node& node) const { return node.next != memory.begin(); }
  __forceinline bool islastfree(const Node& node) const { return node.next == memory.end(); }
  __forceinline bool isfirstfree(const Node& node) const { return node.prior == memory.end(); }
  __forceinline void setlastfree(Node& node) { node.next = memory.end(); }
  __forceinline void setfirstfree(Node& node) { node.prior = memory.end(); }
  __forceinline void setused(Node& node) { node.next = memory.begin(); }

  __forceinline iterator_t firstfree() { return memory.begin()->second.next; }
  __forceinline iterator_t lastfree() { return memory.rbegin()->second.prior; }
  void insertafter(iterator_t place, iterator_t node);
  void remove(iterator_t node);
  iterator_t merge(iterator_t low, iterator_t high);

 public:
  SmallHeap() : pool(nullptr), length(0), total_free(0) {}
  SmallHeap(void* base, size_t length)
      : pool(base), length(length), total_free(length) {
    assert(pool != nullptr && "Invalid base address.");
    assert(pool != (void*)0xFFFFFFFFFFFFFFFFull && "Invalid base address.");
    assert((char*)pool + length != (char*)0xFFFFFFFFFFFFFFFFull && "Invalid pool bounds.");

    Node& start = memory[0];
    Node& node = memory[pool];
    Node& end = memory[(void*)0xFFFFFFFFFFFFFFFFull];

    start.len = 0;
    start.next = memory.find(pool);
    setfirstfree(start);

    node.len = length;
    node.prior = memory.begin();
    node.next = --memory.end();

    end.len = 0;
    end.prior = start.next;
    setlastfree(end);

    high.insert((void*)0xFFFFFFFFFFFFFFFFull);
  }

  void* alloc(size_t bytes);
  void* alloc_high(size_t bytes);
  void free(void* ptr);

  void* base() const { return pool; }
  size_t size() const { return length; }
  size_t remaining() const { return total_free; }
  void* high_split() const { return *high.begin(); }
};

#endif
