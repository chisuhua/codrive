#pragma once

namespace utils
{

// using uint32 = unsigned int;

// static std::mutex mutex_;

/*
void OnAssertFailed(const char* msg, const char* func, const char* file, uint32 line)
{
  std::lock_guard<std::mutex> locker(mutex_);
  fatal("%s in function %s (%s:%u)", msg, func, file, line);
}

void OnPanicReached(const char* msg, const char* func, const char* file, uint32 line)
{
  std::lock_guard<std::mutex> locker(mutex_);
  fatal("%s in function %s (%s:%u)", msg, func, file, line);
}
*/

#define Assert(expr)                                                                                                   \
  if (!(expr))                                                                                                         \
  {                                                                                                                    \
    Fatal("Assertion failed: '" #expr "'");                                                                            \
  }
#define AssertMsg(expr, msg)                                                                                           \
  if (!(expr))                                                                                                         \
  {                                                                                                                    \
    Fatal("Assertion failed: '" msg "'");                                                                              \
  }

#if BUILD_CONFIG_DEBUG
#define DebugAssert(expr)                                                                                              \
  if (!(expr))                                                                                                         \
  {                                                                                                                    \
    Fatal("Debug assertion failed: '" #expr "'");                                                                      \
  }
#define DebugAssertMsg(expr, msg)                                                                                      \
  if (!(expr))                                                                                                         \
  {                                                                                                                    \
    Fatal("Debug assertion failed: '" msg "'");                                                                        \
  }

#else
#define DebugAssert(expr)
#define DebugAssertMsg(expr, msg)
#endif

#define Panic(Message) panic("%s in function %s (%s:%s)", "'" Message "'", __FUNCTION__, __FILE__, __LINE__)
#define Fatal(Message) fatal("%s in function %s (%s:%s)", "'" Message "'", __FUNCTION__, __FILE__, __LINE__)
#define Warning(Message) warning("%s in function %s (%s:%s)", "'" Message "'", __FUNCTION__, __FILE__, __LINE__)

}
