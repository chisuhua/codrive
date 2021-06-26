#pragma once

// Compiler detection
#if defined(_MSC_VER)
#define _COMPILER_MSVC 1
#elif defined(EMSCRIPTEN) || defined(__EMSCRIPTEN__)
#define _COMPILER_EMSCRIPTEN 1
#elif defined(__clang__)
#define _COMPILER_CLANG 1
#elif defined(__GNUC__)
#define _COMPILER_GCC 1
#else
#error Could not detect compiler.
#endif

// disable warnings that show up at warning level 4
#ifdef _COMPILER_MSVC
#pragma warning(disable : 4201) // warning C4201: nonstandard extension used : nameless struct/union
#pragma warning(disable : 4100) // warning C4100: 'Platform' : unreferenced formal parameter
#pragma warning(disable : 4355) // warning C4355: 'this' : used in base member initializer list
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS                                                                                        \
  1 // warning C4996: 'vsnprintf': This function or variable may be unsafe. Consider using vsnprintf_s instead. To
    // disable deprecation, use _CRT_SECURE_NO_WARNINGS. See online help for details.
#endif
#ifndef _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE                                                                                      \
  1 // warning C4996: 'stricmp': The POSIX name for this item is deprecated. Instead, use the ISO C++ conformant name:
    // _stricmp. See online help for details.
#endif
#endif

// The one file we use from the CRT for all other includes.
#include <climits>
#include <cstdarg>
#include <cstddef>
#include <cstdlib>
#include <ctime>

// Standard Template Library includes, only pull in a minimal set of utility methods.
#include <algorithm>
#include <functional>
#include <utility>

// OS Detection
#if defined(EMSCRIPTEN) || defined(__EMSCRIPTEN__)
#define _PLATFORM_HTML5 1
#define _PLATFORM_STR "HTML5"
#elif defined(ANDROID) || defined(__ANDROID__)
#define _PLATFORM_ANDROID 1
#define _PLATFORM_STR "Android"
#elif defined(WIN32) || defined(_WIN32)
#define _PLATFORM_WINDOWS 1
#define _PLATFORM_STR "Windows"
#elif defined(__linux__)
#define _PLATFORM_POSIX 1
#define _PLATFORM_LINUX 1
#define _PLATFORM_STR "Linux"
#elif defined(__APPLE__)
#define _PLATFORM_POSIX 1
#define _PLATFORM_OSX 1
#define _PLATFORM_STR "OSX"
#else
#error Could not detect OS.
#endif

// CPU Detection
#if defined(_COMPILER_MSVC)
#ifdef _M_X64
#define _CPU_X64 1
#define _CPU_STR "x64"
//#define _CPU_SSE_LEVEL 2
#define _CPU_SSE_LEVEL 0
#else
#define _CPU_X86 1
#define _CPU_STR "x86"
/*#if _M_IX86_FP >= 2
    #define _CPU_SSE_LEVEL 2
#elif _M_IX86_FP == 1
    #define _CPU_SSE_LEVEL 1
#else
    #define _CPU_SSE_LEVEL 0
#endif*/
#define _CPU_SSE_LEVEL 0
#endif
#elif defined(_COMPILER_GCC) || defined(_COMPILER_CLANG)
#if defined(__x86_64__)
#define _CPU_X64 1
#define _CPU_STR "x64"
//#define _CPU_SSE_LEVEL 2
#define _CPU_SSE_LEVEL 0
#elif defined(__i386__)
#define _CPU_X86 1
#define _CPU_STR "x86"
#define _CPU_SSE_LEVEL 0
#elif defined(__arm__)
#define _CPU_ARM 1
#define _CPU_STR "ARM"
#define _CPU_SSE_LEVEL 0
#else
#error Could not detect CPU.
#endif
#elif defined(_COMPILER_EMSCRIPTEN)
#define _CPU_STR "JS"
#define _CPU_SSE_LEVEL 0
#else
#error Could not detect CPU.
#endif

// CPU Features
#if defined(_CPU_X86) || defined(_CPU_X64)
#if _CPU_SSE_LEVEL > 0
#define _CPU_FEATURES_STR "+SSE"
#define _SSE_ALIGNMENT 16
#else
#define _CPU_FEATURES_STR ""
#endif
#else
#define _CPU_FEATURES_STR ""
#endif

// Config type
#if defined(_DEBUGFAST)
#define _BUILD_CONFIG_DEBUG 1
#define _BUILD_CONFIG_DEBUGFAST 1
#define _BUILD_CONFIG_STR "DebugFast"
#elif defined(_DEBUG)
#define _BUILD_CONFIG_DEBUG 1
#define _BUILD_CONFIG_STR "Debug"
#elif defined(_SHIPPING)
#define _BUILD_CONFIG_RELEASE 1
#define _BUILD_CONFIG_SHIPPING 1
#define _BUILD_CONFIG_STR "Shipping"
#else
#define _BUILD_CONFIG_RELEASE 1
#define _BUILD_CONFIG_STR "Release"
#endif

// Include pthread header on unix platforms.
#if defined(_PLATFORM_POSIX)
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#endif

// Pull in emscripten.h everywhere
#if defined(_PLATFORM_HTML5)
#include <emscripten.h>
#endif

// Pull in config header if there is one
#if defined(HAVE_MSVC_CONFIG_H)
#include "config-msvc.h"
#elif defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#if defined(_COMPILER_GCC) || defined(_COMPILER_CLANG) || defined(_COMPILER_EMSCRIPTEN)

// Provide the MemoryBarrier intrinsic
//#define MemoryBarrier() asm volatile("" ::: "memory")
#define MemoryBarrier() __sync_synchronize()

#endif

// Deprecated Macro
#ifdef _COMPILER_MSVC
#define DEPRECATED_DECL(Msg) __declspec(deprecated(Msg))
#else
#define DEPRECATED_DECL(Msg)
#endif

// Align Macro
#if defined(_COMPILER_MSVC)
#define ALIGN_DECL(__x) __declspec(align(__x))
#elif defined(_COMPILER_GCC) || defined(_COMPILER_CLANG)
#define ALIGN_DECL(__x) __attribute__((aligned(__x)))
#elif defined(_COMPILER_EMSCRIPTEN)
#define ALIGN_DECL(__x)
#else
#error Invalid compiler
#endif

// Thread local declaration
#if defined(_COMPILER_MSVC)
#define _DECLARE_THREAD_LOCAL(decl) static __declspec(thread) decl
#elif defined(_COMPILER_GCC) || defined(_COMPILER_CLANG)
#define _DECLARE_THREAD_LOCAL(decl) static __thread decl
#elif defined(_COMPILER_EMSCRIPTEN)
#define _DECLARE_THREAD_LOCAL(decl) static decl
#else
#error Invalid compiler
#endif

#ifdef _COMPILER_MSVC

#define MULTI_STATEMENT_MACRO_BEGIN                                                                                    \
  do                                                                                                                   \
  {

#define MULTI_STATEMENT_MACRO_END                                                                                      \
  __pragma(warning(push)) __pragma(warning(disable : 4127))                                                            \
  }                                                                                                                    \
  while (0)                                                                                                            \
  __pragma(warning(pop))

#else

#define MULTI_STATEMENT_MACRO_BEGIN                                                                                    \
  do                                                                                                                   \
  {

#define MULTI_STATEMENT_MACRO_END                                                                                      \
  }                                                                                                                    \
  while (0)

#endif

// countof macro
#ifndef countof
#ifdef _countof
#define countof _countof
#else
template<typename T, size_t N>
char (&__countof_ArraySizeHelper(T (&array)[N]))[N];
#define countof(array) (sizeof(__countof_ArraySizeHelper(array)))
#endif
#endif

// offsetof macro
#ifndef offsetof
#define offsetof(st, m) ((size_t)((char*)&((st*)(0))->m - (char*)0))
#endif

// alignment macro
#define ALIGNED_SIZE(size, alignment) ((size + (decltype(size))((alignment)-1)) & ~((decltype(size))((alignment)-1)))

// containing structure address, in windows terms CONTAINING_RECORD. have to use C cast because otherwise const will
// break it.
#define CONTAINING_STRUCTURE(address, structure, field) ((structure*)(((byte*)(address)) - offsetof(structure, field)))

// stringify macro
#ifndef STRINGIFY
#define STRINGIFY(x) #x
#endif

// unreferenced parameter macro
#ifndef UNREFERENCED_PARAMETER
#if defined(_COMPILER_MSVC)
#define UNREFERENCED_PARAMETER(P) (P)
#elif defined(_COMPILER_GCC) || defined(_COMPILER_CLANG) || defined(_COMPILER_EMSCRIPTEN)
#define UNREFERENCED_PARAMETER(P) (void)(P)
#else
#define UNREFERENCED_PARAMETER(P) (P)
#endif
#endif

// alloca on gcc requires alloca.h
#if defined(_COMPILER_MSVC)
#include <malloc.h>
#elif defined(_COMPILER_GCC) || defined(_COMPILER_CLANG) || defined(_COMPILER_EMSCRIPTEN)
#include <alloca.h>
#endif

// allow usage of std::move/forward without the namespace
using std::forward;
using std::move;

// templated helpers
template<typename T>
static inline T Min(T a, T b)
{
  return (a < b) ? a : b;
}
template<typename T>
static inline T Max(T a, T b)
{
  return (a > b) ? a : b;
}
template<typename T>
static inline void Swap(T& rt1, T& rt2)
{
  T tmp(move(rt1));
  rt1 = rt2;
  rt2 = tmp;
}

// helper to make a class non-copyable
#if 1 // C++11
#define DeclareNonCopyable(ClassType)                                                                                  \
private:                                                                                                               \
  ClassType(const ClassType&) = delete;                                                                                \
  ClassType& operator=(const ClassType&) = delete;

#define DeclareFastCopyable(ClassType)                                                                                 \
public:                                                                                                                \
  ClassType(const ClassType& _copy_) { std::memcpy(this, &_copy_, sizeof(*this)); }                                       \
  ClassType(const ClassType&& _copy_) { std::memcpy(this, &_copy_, sizeof(*this)); }                                      \
  ClassType& operator=(const ClassType& _copy_) { std::memcpy(this, &_copy_, sizeof(*this)); }                            \
  ClassType& operator=(const ClassType&& _copy_) { std::memcpy(this, &_copy_, sizeof(*this)); }

#else
#define DeclareNonCopyable(ClassType)                                                                                  \
private:                                                                                                               \
  ClassType(const ClassType&);                                                                                         \
  ClassType& operator=(const ClassType&);

#define DeclareFastCopyable(ClassType)                                                                                 \
public:                                                                                                                \
  ClassType(const ClassType& _copy_) { std::memcpy(this, &_copy_, sizeof(*this)); }                                       \
  ClassType& operator=(const ClassType& _copy_) { std::memcpy(this, &_copy_, sizeof(*this)); }

#endif
