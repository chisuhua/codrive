#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace hcs {
namespace common {

template<typename LockType>
class ReaderLockGuard final {
public:
  explicit ReaderLockGuard(LockType &lock):
    lock_(lock)
  {
    lock_.ReaderLock();
  }

  ~ReaderLockGuard()
  {
    lock_.ReaderUnlock();
  }

private:
  ReaderLockGuard(const ReaderLockGuard&);
  ReaderLockGuard& operator=(const ReaderLockGuard&);

  LockType &lock_;
};

template<typename LockType>
class WriterLockGuard final {
public:
  explicit WriterLockGuard(LockType &lock):
    lock_(lock)
  {
    lock_.WriterLock();
  }

  ~WriterLockGuard()
  {
    lock_.WriterUnlock();
  }

private:
  WriterLockGuard(const WriterLockGuard&);
  WriterLockGuard& operator=(const WriterLockGuard&);

  LockType &lock_;
};

class ReaderWriterLock final {
public:
  ReaderWriterLock():
    readers_count_(0), writers_count_(0), writers_waiting_(0) {}

  ~ReaderWriterLock() {}

  void ReaderLock();

  void ReaderUnlock();

  void WriterLock();

  void WriterUnlock();

private:
  ReaderWriterLock(const ReaderWriterLock&);
  ReaderWriterLock& operator=(const ReaderWriterLock&);

  size_t readers_count_;
  size_t writers_count_;
  size_t writers_waiting_;
  std::mutex internal_lock_;
  std::condition_variable_any readers_condition_;
  std::condition_variable_any writers_condition_;
};

} // namespace common
} // namespace hcs // hsa

