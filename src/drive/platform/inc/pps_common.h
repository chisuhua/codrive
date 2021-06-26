// The following set of header files provides definitions for GPU
// Architecture:
//   - hsa_common.h
//   - hsa_elf.h
//   - hsa_kernel_code.h
//   - hsa_queue.h
//   - hsa_signal.h
//
// Refer to "HSA Application Binary Interface: AMD GPU Architecture" for more
// information.

#pragma once

#include <stddef.h>
#include <stdint.h>

// Descriptive version of the HSA Application Binary Interface.
#define HCS_ABI_VERSION "HCS Architecture v0.01"

// Alignment attribute that specifies a minimum alignment (in bytes) for
// variables of the specified type.
#if defined(__GNUC__)
#  define __ALIGNED__(x) __attribute__((aligned(x)))
#elif defined(_MSC_VER)
#  define __ALIGNED__(x) __declspec(align(x))
#elif defined(RC_INVOKED)
#  define __ALIGNED__(x)
#else
#  error
#endif

// Creates enumeration entries for packed types. Enumeration entries include
// bit shift amount, bit width, and bit mask.
#define HCS_BITS_CREATE_ENUM_ENTRIES(name, shift, width)                   \
  name ## _SHIFT = (shift),                                                    \
  name ## _WIDTH = (width),                                                    \
  name = (((1 << (width)) - 1) << (shift))                                     \

// Gets bits for specified mask from specified src packed instance.
#define HCS_BITS_GET(src, mask)                                            \
  ((src & mask) >> mask ## _SHIFT)                                             \

// Sets val bits for specified mask in specified dst packed instance.
#define HCS_BITS_SET(dst, mask, val)                                       \
  dst &= (~(1 << mask ## _SHIFT) & ~mask);                                     \
  dst |= (((val) << mask ## _SHIFT) & mask)                                    \

