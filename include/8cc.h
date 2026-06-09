// Copyright 2014 Rui Ueyama. Released under the MIT license.

// Target architecture selection
// Default: x86-64; define __EX_ISA__ to switch to EX_ISA target
// (defined by cpp_init() based on -m flag)

#ifdef __EX_ISA__
  // EX_ISA target (4-bit opcodes, 16-bit words, 256-byte address space)
  #define __EX_ISA__ 1
  #define __16_BIT__ 1
  #define _ILP32 1
  #define __SIZEOF_INT__ 2
  #define __SIZEOF_LONG__ 2
  #define __SIZEOF_LONG_LONG__ 4
  #define __SIZEOF_POINTER__ 2
  #define __SIZEOF_PTRDIFF_T__ 2
  #define __SIZEOF_SIZE_T__ 2
  #undef __SIZEOF_DOUBLE__
  #undef __SIZEOF_FLOAT__
  #undef __SIZEOF_LONG_DOUBLE__
#else
  // x86-64 target (default)
  #define _LP64 1
  #define __SIZEOF_DOUBLE__ 8
  #define __SIZEOF_FLOAT__ 4
  #define __SIZEOF_INT__ 4
  #define __SIZEOF_LONG_DOUBLE__ 8
  #define __SIZEOF_LONG_LONG__ 8
  #define __SIZEOF_LONG__ 8
  #define __SIZEOF_POINTER__ 8
  #define __SIZEOF_PTRDIFF_T__ 8
  #define __SIZEOF_SHORT__ 2
  #define __SIZEOF_SIZE_T__ 8
  #define __amd64 1
  #define __amd64__ 1
  #define __x86_64 1
  #define __x86_64__ 1
#endif

// Common definitions for all targets
#define __8cc__ 1
#define __ELF__ 1
#define __SIZEOF_SHORT__ 2
#define __STDC_HOSTED__ 1
#define __STDC_ISO_10646__ 201103L
#define __STDC_NO_ATOMICS__ 1
#define __STDC_NO_COMPLEX__ 1
#define __STDC_NO_THREADS__ 1
#define __STDC_NO_VLA__ 1
#define __STDC_UTF_16__ 1
#define __STDC_UTF_32__ 1
#define __STDC_VERSION__ 201112L
#define __STDC__ 1
#define __gnu_linux__ 1
#define __linux 1
#define __linux__ 1
#define __unix 1
#define __unix__ 1
#define linux 1

#define __alignof__ alignof
#define __const__ const
#define __inline__ inline
#define __restrict restrict
#define __restrict__ restrict
#define __signed__ signed
#define __typeof__ typeof
#define __volatile__ volatile

typedef unsigned short char16_t;
typedef unsigned int char32_t;
