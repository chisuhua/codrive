#pragma once

#include "stdint.h"
#include "stddef.h"
#include "stdlib.h"
#include <assert.h>
#include <iostream>
#include <string>
#include <algorithm>

typedef unsigned int uint;
typedef uint64_t uint64;

#if defined(__GNUC__)
#if defined(__i386__) || defined(__x86_64__)
#include <x86intrin.h>
#endif

#define __forceinline __inline__ __attribute__((always_inline))
#define __declspec(x) __attribute__((x))
#undef __stdcall
#define __stdcall  // __attribute__((__stdcall__))
#define __ALIGNED__(x) __attribute__((aligned(x)))

static __forceinline void* _aligned_malloc(size_t size, size_t alignment) {
#ifdef _ISOC11_SOURCE
  return aligned_alloc(alignment, size);
#else
  void *mem = NULL;
  if (NULL != posix_memalign(&mem, alignment, size))
    return NULL;
  return mem;
#endif
}
static __forceinline void _aligned_free(void* ptr) { return free(ptr); }
#elif defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include "intrin.h"
#define __ALIGNED__(x) __declspec(align(x))
#if (_MSC_VER < 1800)  // < VS 2013
static __forceinline unsigned long long int strtoull(const char* str,
                                                     char** endptr, int base) {
  return static_cast<unsigned long long>(_strtoui64(str, endptr, base));
}
#endif

#if (_MSC_VER < 1900)  // < VS 2015
#define thread_local __declspec(thread)
#endif
#else
#error "Compiler and/or processor not identified."
#endif

// A macro to disallow the copy and move constructor and operator= functions
#define DISALLOW_COPY_AND_ASSIGN(TypeName)                                                         \
  TypeName(const TypeName&) = delete;                                                              \
  TypeName(TypeName&&) = delete;                                                                   \
  void operator=(const TypeName&) = delete;                                                        \
  void operator=(TypeName&&) = delete;

template <typename lambda>
class ScopeGuard {
 public:
  explicit __forceinline ScopeGuard(const lambda& release)
      : release_(release), dismiss_(false) {}

  ScopeGuard(ScopeGuard& rhs) { *this = rhs; }

  __forceinline ~ScopeGuard() {
    if (!dismiss_) release_();
  }
  __forceinline ScopeGuard& operator=(ScopeGuard& rhs) {
    dismiss_ = rhs.dismiss_;
    release_ = rhs.release_;
    rhs.dismiss_ = true;
  }
  __forceinline void Dismiss() { dismiss_ = true; }

 private:
  lambda release_;
  bool dismiss_;
};

template <typename lambda>
static __forceinline ScopeGuard<lambda> MakeScopeGuard(lambda rel) {
  return ScopeGuard<lambda>(rel);
}

#define MAKE_SCOPE_GUARD_HELPER(lname, sname, ...) \
  auto lname = __VA_ARGS__;                        \
  ScopeGuard<decltype(lname)> sname(lname);
#define MAKE_SCOPE_GUARD(...)                                   \
  MAKE_SCOPE_GUARD_HELPER(PASTE(scopeGuardLambda, __COUNTER__), \
                          PASTE(scopeGuard, __COUNTER__), __VA_ARGS__)
#define MAKE_NAMED_SCOPE_GUARD(name, ...)                             \
  MAKE_SCOPE_GUARD_HELPER(PASTE(scopeGuardLambda, __COUNTER__), name, \
                          __VA_ARGS__)


/// @brief: Free the memory space which is newed previously.
/// @param: ptr(Input), a pointer to memory space. Can't be NULL.
/// @return: void.
struct DeleteObject {
  template <typename T>
  void operator()(const T* ptr) const {
    delete ptr;
  }
};

static __forceinline bool strIsEmpty(const char* str) noexcept { return str[0] == '\0'; }

static __forceinline std::string& ltrim(std::string& s) {
  auto it = std::find_if(s.begin(), s.end(),
                         [](char c) { return !std::isspace<char>(c, std::locale::classic()); });
  s.erase(s.begin(), it);
  return s;
}

static __forceinline std::string& rtrim(std::string& s) {
  auto it = std::find_if(s.rbegin(), s.rend(),
                         [](char c) { return !std::isspace<char>(c, std::locale::classic()); });
  s.erase(it.base(), s.end());
  return s;
}

static __forceinline std::string& trim(std::string& s) { return ltrim(rtrim(s)); }



#include "atomic_helpers.h"

