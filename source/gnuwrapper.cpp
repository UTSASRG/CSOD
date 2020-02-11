// -*- C++ -*-

/**
 * @file   gnuwrapper.cpp
 * @brief  Replaces malloc family on GNU/Linux with custom versions.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 * @note   Copyright (C) 2010 by Emery Berger, University of Massachusetts Amherst.
 */


#ifndef __GNUC__
#error "This file requires the GNU compiler."
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h> // for memcpy and memset
#include <stdint.h>
#include <errno.h>

#include <malloc.h>
#include <pthread.h>
#include <sys/cdefs.h>

#include <new>

/*
   To use this library,
   you only need to define the following allocation functions:

   - xxmalloc
   - xxfree
   - xxmalloc_usable_size
   - xxmalloc_lock
   - xxmalloc_unlock

   See the extern "C" block below for function prototypes and more
   details. YOU SHOULD NOT NEED TO MODIFY ANY OF THE CODE HERE TO
   SUPPORT ANY ALLOCATOR.


LIMITATIONS:

- This wrapper assumes that the underlying allocator will do "the
right thing" when xxfree() is called with a pointer internal to an
allocated object. Header-based allocators, for example, need not
apply.

*/

#define WEAK(x) __attribute__ ((weak, alias(#x)))
#ifndef __THROW
#define __THROW
#endif

#define CUSTOM_PREFIX(x) custom##x

#define WEAK_REDEF1(type,fname,arg1) type fname(arg1) __THROW WEAK(custom##fname)
#define WEAK_REDEF2(type,fname,arg1,arg2) type fname(arg1,arg2) __THROW WEAK(custom##fname)
#define WEAK_REDEF3(type,fname,arg1,arg2,arg3) type fname(arg1,arg2,arg3) __THROW WEAK(custom##fname)

extern "C" {
  WEAK_REDEF1(void *, malloc, size_t);
  WEAK_REDEF1(void, free, void *);
  WEAK_REDEF1(void, cfree, void *);
  WEAK_REDEF2(void *, calloc, size_t, size_t);
  WEAK_REDEF2(void *, realloc, void *, size_t);
  WEAK_REDEF2(void *, memalign, size_t, size_t);
  WEAK_REDEF3(int, posix_memalign, void **, size_t, size_t);
  WEAK_REDEF2(void *, aligned_alloc, size_t, size_t);
  WEAK_REDEF1(size_t, malloc_usable_size, void *);

  // real function
  void * xxmalloc (size_t);
  void   xxfree (void *);

  void * xxmemalign(size_t alignment, size_t size);

  // Takes a pointer and returns how much space it holds.
  size_t xxmalloc_usable_size (void *);

  void   xxmalloc_remove_watchpoint (void *);
  bool   xxmalloc_install_watchpoint (void *, size_t, int);
#ifdef ENABLE_EVIDENCE
  void   xxmalloc_updateheader (void *, size_t);
#endif
}

extern inline void enableCauser();
extern inline void disableCauser();
extern inline bool isCauser();

#define CUSTOM_MALLOC(x)     CUSTOM_PREFIX(malloc)(x)
#define CUSTOM_FREE(x)       CUSTOM_PREFIX(free)(x)
#define CUSTOM_CFREE(x)      CUSTOM_PREFIX(cfree)(x)
#define CUSTOM_REALLOC(x,y)  CUSTOM_PREFIX(realloc)(x,y)
#define CUSTOM_CALLOC(x,y)   CUSTOM_PREFIX(calloc)(x,y)
#define CUSTOM_MEMALIGN(x,y) CUSTOM_PREFIX(memalign)(x,y)
#define CUSTOM_POSIX_MEMALIGN(x,y,z) CUSTOM_PREFIX(posix_memalign)(x,y,z)
#define CUSTOM_ALIGNED_ALLOC(x,y) CUSTOM_PREFIX(aligned_alloc)(x,y)
#define CUSTOM_GETSIZE(x)    CUSTOM_PREFIX(malloc_usable_size)(x)
#define CUSTOM_GOODSIZE(x)    CUSTOM_PREFIX(malloc_good_size)(x)
#define CUSTOM_VALLOC(x)     CUSTOM_PREFIX(valloc)(x)
#define CUSTOM_PVALLOC(x)    CUSTOM_PREFIX(pvalloc)(x)
#define CUSTOM_RECALLOC(x,y,z)   CUSTOM_PREFIX(recalloc)(x,y,z)
#define CUSTOM_STRNDUP(s,sz) CUSTOM_PREFIX(strndup)(s,sz)
#define CUSTOM_STRDUP(s)     CUSTOM_PREFIX(strdup)(s)
#define CUSTOM_GETCWD(b,s)   CUSTOM_PREFIX(getcwd)(b,s)

// GNU-related routines:
#define CUSTOM_MALLOPT(x,y)         CUSTOM_PREFIX(mallopt)(x,y)
#define CUSTOM_MALLOC_TRIM(s)       CUSTOM_PREFIX(malloc_trim)(s)
#define CUSTOM_MALLOC_STATS(a)      CUSTOM_PREFIX(malloc_stats)(a)
#define CUSTOM_MALLOC_GET_STATE(p)  CUSTOM_PREFIX(malloc_get_state)(p)
#define CUSTOM_MALLOC_SET_STATE(p)  CUSTOM_PREFIX(malloc_set_state)(p)
#define CUSTOM_MALLINFO(a)          CUSTOM_PREFIX(mallinfo)(a)

#define MYCDECL

extern "C" void MYCDECL CUSTOM_FREE (void * ptr) {
  xxfree (ptr);
}

extern "C" void * MYCDECL CUSTOM_MALLOC(size_t sz) {
  if (sz >> (sizeof(size_t) * 8 - 1)) {
    return NULL;
  }
  void * ptr = xxmalloc(sz);
  return ptr;
}

extern "C" void * MYCDECL CUSTOM_CALLOC(size_t nelem, size_t elsize) {
  size_t n = nelem * elsize;
  if (elsize && nelem != n / elsize) {
    return NULL;
  }

  bool watching = isCauser();
  if(watching){
    disableCauser();
  }

  void * ptr = CUSTOM_MALLOC(n);
  // Zero out the malloc'd block.
  if (ptr != NULL) {
    memset (ptr, 0, n);
  }

  if(watching){
    enableCauser();
  }

  return ptr;
}


#if !defined(_WIN32)
extern "C" void * MYCDECL CUSTOM_MEMALIGN (size_t alignment, size_t size)
#if !defined(__FreeBSD__) && !defined(__SVR4)
throw()
#endif
  ;

  extern "C" int CUSTOM_POSIX_MEMALIGN (void **memptr, size_t alignment, size_t size)
#if !defined(__FreeBSD__) && !defined(__SVR4)
throw()
#endif
{
  // Check for non power-of-two alignment.
  if ((alignment == 0) ||
      (alignment & (alignment - 1)))
  {
    return EINVAL;
  }
  void * ptr = CUSTOM_MEMALIGN (alignment, size);
  if (!ptr) {
    return ENOMEM;
  } else {
    *memptr = ptr;
    return 0;
  }
}
#endif

extern "C" void * MYCDECL CUSTOM_MEMALIGN (size_t alignment, size_t size)
#if !defined(__FreeBSD__) && !defined(__SVR4)
throw()
#endif
{
  return xxmemalign(alignment, size);
}

extern "C" void * MYCDECL CUSTOM_ALIGNED_ALLOC(size_t alignment, size_t size)
#if !defined(__FreeBSD__)
throw()
#endif
{
  // Per the man page: "The function aligned_alloc() is the same as
  // memalign(), except for the added restriction that size should be
  // a multiple of alignment." Rather than check and potentially fail,
  // we just enforce this by rounding up the size, if necessary.
  size = size + alignment - (size % alignment);
  return CUSTOM_MEMALIGN(alignment, size);
}

extern "C" size_t MYCDECL CUSTOM_GETSIZE (void * ptr) {
  return xxmalloc_usable_size (ptr);
}

extern "C" void MYCDECL CUSTOM_CFREE (void * ptr) {
  xxfree (ptr);
}

extern "C" size_t MYCDECL CUSTOM_GOODSIZE (size_t sz) {
  void * ptr = CUSTOM_MALLOC(sz);
  size_t objSize = CUSTOM_GETSIZE(ptr);
  CUSTOM_FREE(ptr);
  return objSize;
}

extern "C" void * MYCDECL CUSTOM_REALLOC (void * ptr, size_t sz) {
  if (ptr == NULL) {
    ptr = CUSTOM_MALLOC (sz);
    return ptr;
  }
  if (sz == 0) {
    CUSTOM_FREE (ptr);
#if defined(__APPLE__)
    // 0 size = free. We return a small object.  This behavior is
    // apparently required under Mac OS X and optional under POSIX.
    return CUSTOM_MALLOC(1);
#else
    // For POSIX, don't return anything.
    return NULL;
#endif
  }

  // remove watchpoint first
  xxmalloc_remove_watchpoint(ptr);

  size_t objSize = CUSTOM_GETSIZE (ptr);

  void * buf = CUSTOM_MALLOC(sz);

  if (buf != NULL) {
    if (objSize == CUSTOM_GETSIZE(buf)) {
      // The objects are the same actual size.
      // Free the new object and return the original.
      CUSTOM_FREE (buf);

#ifdef ENABLE_EVIDENCE
      xxmalloc_updateheader(ptr, sz);
#endif
      // install watchpoint to new position
      xxmalloc_install_watchpoint(ptr, sz, 0);
      return ptr;
    }

    // Copy the contents of the original object
    // up to the size of the new block.
    size_t minSize = (objSize < sz) ? objSize : sz;
    memcpy (buf, ptr, minSize);
  }

  // Free the old block.
  CUSTOM_FREE (ptr);

  // Return a pointer to the new one.
  return buf;
}

#if defined(__linux)

extern "C" char * MYCDECL CUSTOM_STRNDUP(const char * s, size_t sz) {
  char * newString = NULL;
  if (s != NULL) {
    size_t cappedLength = strnlen (s, sz);
    if ((newString = (char *) CUSTOM_MALLOC(cappedLength + 1))) {
      strncpy(newString, s, cappedLength);
      newString[cappedLength] = '\0';
    }
  }
  return newString;
}
#endif

extern "C" char * MYCDECL CUSTOM_STRDUP(const char * s) {
  char * newString = NULL;
  if (s != NULL) {
    if ((newString = (char *) CUSTOM_MALLOC(strlen(s) + 1))) {
      strcpy(newString, s);
    }
  }
  return newString;
}

#if !defined(_WIN32)
#include <dlfcn.h>
#include <limits.h>

#if !defined(RTLD_NEXT)
#define RTLD_NEXT ((void *) -1)
#endif

typedef char * getcwdFunction (char *, size_t);

extern "C"  char * MYCDECL CUSTOM_GETCWD(char * buf, size_t size) {
  static getcwdFunction * real_getcwd
    = reinterpret_cast<getcwdFunction *>
    (reinterpret_cast<uintptr_t>(dlsym (RTLD_NEXT, "getcwd")));

  if (!buf) {
    if (size == 0) {
      size = PATH_MAX;
    }
    buf = (char *) CUSTOM_MALLOC(size);
  }
  return (real_getcwd)(buf, size);
}

#endif

extern "C" int  CUSTOM_MALLOPT (int /* param */, int /* value */) {
  // NOP.
  return 1; // success.
}

extern "C" int CUSTOM_MALLOC_TRIM(size_t /* pad */) {
  // NOP.
  return 0; // no memory returned to OS.
}

extern "C" void CUSTOM_MALLOC_STATS(void) {
  // NOP.
}

extern "C" void * CUSTOM_MALLOC_GET_STATE(void) {
  return NULL; // always returns "error".
}

extern "C" int CUSTOM_MALLOC_SET_STATE(void * /* ptr */) {
  return 0; // success.
}

#if defined(__GNUC__) && !defined(__FreeBSD__)
extern "C" struct mallinfo CUSTOM_MALLINFO(void) {
  // For now, we return useless stats.
  struct mallinfo m;
  m.arena = 0;
  m.ordblks = 0;
  m.smblks = 0;
  m.hblks = 0;
  m.hblkhd = 0;
  m.usmblks = 0;
  m.fsmblks = 0;
  m.uordblks = 0;
  m.fordblks = 0;
  m.keepcost = 0;
  return m;
}
#endif

#if defined(__SVR4)
// Apparently we no longer need to replace new and friends for Solaris.
#define NEW_INCLUDED
#endif

#ifndef NEW_INCLUDED
#define NEW_INCLUDED

void * operator new (size_t sz)
#if defined(_GLIBCXX_THROW)
_GLIBCXX_THROW (std::bad_alloc)
#endif
{
  void * ptr = CUSTOM_MALLOC (sz);
  if (ptr == NULL) {
    throw std::bad_alloc();
  } else {
    return ptr;
  }
}

void operator delete (void * ptr)
#if !defined(linux_)
throw ()
#endif
{
  CUSTOM_FREE (ptr);
}

#if !defined(__SUNPRO_CC) || __SUNPRO_CC > 0x420
void * operator new (size_t sz, const std::nothrow_t&) throw() {
  return CUSTOM_MALLOC(sz);
}

void * operator new[] (size_t size)
#if defined(_GLIBCXX_THROW)
_GLIBCXX_THROW (std::bad_alloc)
#endif
{
  void * ptr = CUSTOM_MALLOC(size);
  if (ptr == NULL) {
    throw std::bad_alloc();
  } else {
    return ptr;
  }
}

  void * operator new[] (size_t sz, const std::nothrow_t&)
throw()
{
  return CUSTOM_MALLOC(sz);
}

void operator delete[] (void * ptr)
#if defined(_GLIBCXX_USE_NOEXCEPT)
  _GLIBCXX_USE_NOEXCEPT
#else
#if defined(__GNUC__)
  // clang + libcxx on linux
  _NOEXCEPT
#endif
#endif
{
  CUSTOM_FREE (ptr);
}

#if defined(__cpp_sized_deallocation) && __cpp_sized_deallocation >= 201309

void operator delete(void * ptr, size_t)
#if !defined(linux_)
throw ()
#endif
{
  CUSTOM_FREE (ptr);
}

void operator delete[](void * ptr, size_t)
#if defined(__GNUC__)
  _GLIBCXX_USE_NOEXCEPT
#endif
{
  CUSTOM_FREE (ptr);
}
#endif

#endif
#endif

/***** replacement functions for GNU libc extensions to malloc *****/

// NOTE: for convenience, we assume page size = 8192.

extern "C" void * MYCDECL CUSTOM_VALLOC (size_t sz)
{
  return CUSTOM_MEMALIGN (8192UL, sz);
}


extern "C" void * MYCDECL CUSTOM_PVALLOC (size_t sz)
{
  // Rounds up to the next pagesize and then calls valloc. Hoard
  // doesn't support aligned memory requests.
  return CUSTOM_VALLOC ((sz + 8191UL) & ~8191UL);
}

// The wacky recalloc function, for Windows.
extern "C" void * MYCDECL CUSTOM_RECALLOC (void * p, size_t num, size_t sz)
{
  void * ptr = CUSTOM_REALLOC (p, num * sz);
  if ((p == NULL) && (ptr != NULL)) {
    // Clear out the memory.
    memset (ptr, 0, CUSTOM_GETSIZE(ptr));
  }
  return ptr;
}

