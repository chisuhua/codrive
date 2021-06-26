#include "inc/locks.h"

// namespace amd {
namespace hcs {
namespace common {

void ReaderWriterLock::ReaderLock()
{
  internal_lock_.lock();
  while (0 < writers_count_) {
    readers_condition_.wait(internal_lock_);
  }
  readers_count_ += 1;
  internal_lock_.unlock();
}

void ReaderWriterLock::ReaderUnlock()
{
  internal_lock_.lock();
  readers_count_ -= 1;
  if (0 == readers_count_ && 0 < writers_waiting_) {
    writers_condition_.notify_one();
  }
  internal_lock_.unlock();
}

void ReaderWriterLock::WriterLock()
{
  internal_lock_.lock();
  writers_waiting_ += 1;
  while (0 < readers_count_ || 0 < writers_count_) {
    writers_condition_.wait(internal_lock_);
  }
  writers_count_ += 1;
  writers_waiting_ -= 1;
  internal_lock_.unlock();
}

void ReaderWriterLock::WriterUnlock()
{
  internal_lock_.lock();
  writers_count_ -= 1;
  if (0 < writers_waiting_) {
    writers_condition_.notify_one();
  }
  readers_condition_.notify_all();
  internal_lock_.unlock();
}

} // namespace common
} // namespace hcs // hsa
// } // namespace amd
