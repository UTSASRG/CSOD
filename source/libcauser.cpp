#ifndef __GNUC__
#error "This file requires the GNU compiler."
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <pthread.h>
#include <stdarg.h>
#include <netdb.h>

#include "real.hh"
#include "causer.hh"
#include "xdefines.hh"
#include "threadstruct.hh"
#include "xthread.hh"

#include "selfmap.hh"
#include "watchpoint.hh"
#include "objectguard.hh"

// glibc malloc hook
#include "gnuwrapper.cpp"
extern "C" {
  void* xxmalloc(size_t sz);
  void xxfree(void* ptr);
  void xxrealfree(void* ptr);
  size_t xxmalloc_usable_size (void *ptr);

  void *xxmemalign(size_t alignment, size_t size);
  
  void xxmalloc_remove_watchpoint (void *ptr);
  bool xxmalloc_install_watchpoint (void *ptr, size_t sz, int offset);
#ifdef ENABLE_EVIDENCE
  void xxmalloc_updateheader (void *ptr, size_t sz);
#endif
}; // end glibc malloc hook

unsigned long causer_stack_offset;

enum { InitialMallocSize = 1024 * 1024 * 1024 };
static char _buf[InitialMallocSize];
static int _allocated = 0;

#ifdef STATISTICS
unsigned int mallocindex = 0;
unsigned int csindex = 0;
#endif

float boostratio;
__thread thread_t* current;
__thread bool isWatching = false;
// this is used for thread create and installing watchpoint
pthread_rwlock_t rwlock=PTHREAD_RWLOCK_INITIALIZER;
bool funcInitialized = false;
bool libInitialized = false;

#define MAX_FILENAME_LEN 256 
extern char * program_invocation_name;
char outputFile[MAX_FILENAME_LEN];

__attribute__((constructor)) void initializer() {
  //fprintf(stderr, "call initializer\n");
  INIT_REALFUNCTION;

  if(!libInitialized) {
    xthread::getInstance().initialize();
    causer::getInstance().initialize();
    libInitialized = true;
  }
 
  // get file name 
  snprintf(outputFile, MAX_FILENAME_LEN, "%s_callstack.info", program_invocation_name);

  // load history information
  causer::getInstance().loadHistoryInfo(outputFile);
}

void finalizer() {

  disableCauser();
#ifdef ENABLE_EVIDENCE_SCAN_MEMORY
  causer::getInstance().checkAllMemory();
#endif
  //snprintf(outputFile, MAX_FILENAME_LEN, "%s_%ld_callstack.info", program_invocation_name, syscall(__NR_gettid));
  causer::getInstance().saveHistoryInfo(outputFile);
}

typedef int (*main_fn_t)(int, char**, char**);

extern "C" int __libc_start_main(main_fn_t, int, char**, void (*)(), void (*)(), void (*)(), void*) __attribute__((weak, alias("causer_libc_start_main")));

extern "C" int causer_libc_start_main(main_fn_t main_fn, int argc, char** argv, void (*init)(), void (*fini)(), void (*rtld_fini)(), void* stack_end) {

  //fprintf(stderr, "call libc_start_main\n");
  auto real_libc_start_main = (decltype(__libc_start_main)*)dlsym(RTLD_NEXT, "__libc_start_main");

  // Register the exit function
  atexit(finalizer);

  // only for main thread
  current->startFrame = (char *)__builtin_frame_address(0);
  //stackTop = (void*)(((intptr_t)&real_libc_start_main + xdefines::PAGE_SIZE) & ~xdefines::PAGE_SIZE_MASK);

  selfmap::getInstance().getTextRegions();

  fprintf(stderr, "***enable Causer***\n");
  enableCauser();

  return real_libc_start_main(main_fn, argc, argv, init, fini, rtld_fini, stack_end);
} 

// Temporary bump-pointer allocator for malloc() calls before library is initialized
static void* tempmalloc(int size) {
  if(_allocated + size > InitialMallocSize) {
    printf("Not enough space for tempmalloc");
    abort();
  } else {
    void* p = (void*)&_buf[_allocated];
    _allocated += size;
    return p;
  }
}

//********* intercept glibc malloc ***********

void* xxmalloc(size_t sz) {

  //fprintf(stderr, "call malloc sz %zu\n", sz);
  void* ptr = NULL;

  size_t realsize = sz; 
#ifdef ENABLE_EVIDENCE
  realsize += sizeof(objectGuard) + xdefines::SENTINEL_SIZE;
#else
  realsize += xdefines::REDZONESIZE;
#endif

  if(!libInitialized) {
    realsize = (realsize + 7) & ~7;
    ptr = tempmalloc(realsize);
  }
  else {
    ptr = Real::malloc(realsize);
  }

#ifdef ENABLE_EVIDENCE
  objectGuard* o = new (ptr) objectGuard(ptr, sz);
  ptr = o->getStartPtr();
#endif

  //fprintf(stderr, "thread %ld: call malloc sz %zu at %p, header size %lu\n", syscall(__NR_gettid), sz, ptr, sizeof(objectGuard));

  // install watchpoint
  xxmalloc_install_watchpoint(ptr, sz, 0);

  return ptr;
}

void *xxmemalign(size_t alignment, size_t sz){

  INIT_REALFUNCTION;
  size_t realsize = sz; 
#ifdef ENABLE_EVIDENCE
  // make guarder aligned
  size_t objguardsize = (sizeof(objectGuard) + alignment - 1) & ~(alignment - 1);
  realsize += objguardsize + xdefines::SENTINEL_SIZE;
#else
  realsize += xdefines::REDZONESIZE;
#endif

  void* ptr = Real::memalign(alignment, realsize);
#ifdef ENABLE_EVIDENCE
  // set guard before real object
  objectGuard* o = new ((void*)((intptr_t)ptr + objguardsize - sizeof(objectGuard))) objectGuard(ptr, sz);
  ptr = o->getStartPtr();
#endif

  // install watchpoint
  xxmalloc_install_watchpoint(ptr, sz, 0);

  return ptr;
}

void xxfree(void* ptr) {
  //fprintf(stderr, "thread %ld: call free at %p\n", syscall(__NR_gettid), ptr);
  // remove watchpoint first
  xxmalloc_remove_watchpoint(ptr);

  xxrealfree(ptr);
}

void xxrealfree(void* ptr){
  if(ptr && ((ptr > (void *)(_buf + InitialMallocSize))
        || (ptr < (void *)_buf))) {
    //fprintf(stderr, "thread %ld: call real free at %p\n", syscall(__NR_gettid), ptr);
#ifdef ENABLE_EVIDENCE
    ptr = causer::getInstance().checkPointer(ptr);
#endif
    Real::free(ptr);
  }
}

size_t xxmalloc_usable_size (void *ptr){
#ifdef ENABLE_EVIDENCE
  objectGuard* obj = getObjectGuard(ptr);
  ptr = obj->getRealPtr();
#endif

  //FIXME temp malloc
  if((ptr >= (void *)_buf) &&
      (ptr <= (void *)(_buf + InitialMallocSize))) {
    return 1;
  }
  //size_t ret = Real::malloc_usable_size(ptr);
  return Real::malloc_usable_size(ptr); 
}

bool xxmalloc_install_watchpoint (void *ptr, size_t sz, int offset){
  bool ret = false;
  if(isCauser() && ptr){
    disableCauser();
    ret = causer::getInstance().startWatch(ptr, sz-offset);
    enableCauser();
  }
  return ret;
}

#ifdef ENABLE_EVIDENCE
void xxmalloc_updateheader (void *ptr, size_t sz){
  objectGuard* obj = getObjectGuard(ptr);
  obj->setObjectSize(sz);
  obj->setTailSentinel();
}
#endif

void xxmalloc_remove_watchpoint (void *ptr){
  if(isCauser() && ptr){
    causer::getInstance().stopWatch(ptr);
  }
} 
//********* end glibc malloc *****************

int pthread_create(pthread_t * tid, const pthread_attr_t * attr, void *(*start_routine)(void *), void * arg) {
  COND_DISABLE;
  int result = xthread::getInstance().thread_create(tid, attr, start_routine, arg);
  COND_ENABLE;
  return result; 
}

void pthread_exit(void*  /*  value_ptr */){
  // This should probably throw a special exception to be caught in spawn.
  throw PTHREADEXIT_CODE;
}

#undef strlen
// copy code from OpenBSD
size_t strlen(const char *str) {
  const char *s;

  for (s = str; *s; ++s)
    ;
  return (s - str);
}

#undef strcpy
char * strcpy(char *to, const char *from) {
  char *save = to;

  for (; (*to = *from) != '\0'; ++from, ++to);
  return(save);
}

int strcmp(const char *s1, const char *s2) {
  while (*s1 == *s2++)
    if (*s1++ == 0)
      return (0);
  return (*(unsigned char *)s1 - *(unsigned char *)--s2);
}

/*
 *  * Copy src to dst, truncating or null-padding to always copy n bytes.
 *   * Return dst.
 *    */
#undef strncpy
char * strncpy(char *dst, const char *src, size_t n) {
  if (n != 0) {
    char *d = dst;
    const char *s = src;

    do {
      if ((*d++ = *s++) == 0) {
        /*  NUL pad the remaining n-1 bytes */
        while (--n != 0)
          *d++ = 0;
        break;
      }
    } while (--n != 0);
  }
  return (dst);
}

size_t strspn(const char *s1, const char *s2) {
  //fprintf(stderr, "call strspn\n");
  const char *p = s1, *spanp;
  char c, sc;

  /*
   *   * Skip any characters in s2, excluding the terminating \0.
   *     */
cont:
  c = *p++;
  for (spanp = s2; (sc = *spanp++) != 0;)
    if (sc == c)
      goto cont;
  return (p - 1 - s1);
}

size_t strcspn(const char *s1, const char *s2) {
  //fprintf(stderr, "call strcspn\n");
  const char *p, *spanp;
  char c, sc;

  /*
   *   * Stop as soon as we find any character from s2.  Note that there
   *     * must be a NUL in s2; it suffices to stop when we find that, too.
   *       */
  for (p = s1;;) {
    c = *p++;
    spanp = s2;
    do {
      if ((sc = *spanp++) == c)
        return (p - 1 - s1);
    } while (sc != 0);
  }
  /*  NOTREACHED */
}

#undef strdup
char * strdup(const char *str) {
  size_t siz;
  char *copy;

  siz = strlen(str) + 1;
  if ((copy = (char *)malloc(siz)) == NULL)
    return(NULL);
  (void)memcpy(copy, str, siz);
  return(copy);
}

pid_t fork(void){
  disableCauser();
  watchpointObject* obj = watchpoint::getInstance().getAllWatchpointObjects();
  for(int i = 0; i < xdefines::MAX_WATCHPOINTS; i++) {
    watchpoint::getInstance().disableWatchpointByAddr(obj[i].addr);
  }
  pid_t ret = Real::fork();
  if(ret == 0){
    xthread::getInstance().reInitializeAtRuntime();
  }
  enableCauser();
  return ret;
}

/*
*/
FILE* fopen(const char* filename, const char* modes) {
  
  FILE* ret = NULL;

  COND_DISABLE;
  INIT_REALFUNCTION;

  ret = Real::fopen(filename, modes);

  COND_ENABLE;

  return ret;
}

FILE* fopen64(const char* filename, const char* modes) {

  FILE* ret = NULL;

  COND_DISABLE;
  INIT_REALFUNCTION;

  ret = Real::fopen64(filename, modes);

  COND_ENABLE;

  return ret;
}

int open(const char* pathname, int flags, ...) {
  int ret = -1;

  COND_DISABLE;
  INIT_REALFUNCTION;

  int mode;
  if(flags & O_CREAT) {
    va_list arg;
    va_start(arg, flags);
    mode = va_arg(arg, mode_t);
    va_end(arg);
  } else {
    mode = 0;
  }

  ret = Real::open(pathname, flags, mode);

  COND_ENABLE;

  return ret;
}

int open64(const char *pathname, int flags, ...) {
  int ret = -1;

  COND_DISABLE;
  INIT_REALFUNCTION;

  int mode;
  if(flags & O_CREAT) {
    va_list arg;
    va_start(arg, flags);
    mode = va_arg(arg, mode_t);
    va_end(arg);
  } else {
    mode = 0;
  }

  ret = Real::open64(pathname, flags, mode);

  COND_ENABLE;

  return ret;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream){

  size_t ret = 0;

  COND_DISABLE;
  INIT_REALFUNCTION;

  ret = Real::fread(ptr, size, nmemb, stream);

  COND_ENABLE;

  return ret;

}

int backtrace(void **buffer, int size){
  int ret = -1;

  COND_DISABLE;
  INIT_REALFUNCTION;

  ret = Real::backtrace(buffer, size);

  COND_ENABLE;

  return ret;
}

extern "C" {
struct hostent *gethostbyname(const char *name){
  struct hostent * ret = NULL;

  COND_DISABLE;
  INIT_REALFUNCTION;

  ret = Real::gethostbyname(name);

  COND_ENABLE;

  return ret;
}

struct servent *getservbyname(const char *name, const char *proto){
  struct servent * ret = NULL;

  COND_DISABLE;
  INIT_REALFUNCTION;

  ret = Real::getservbyname(name, proto);

  COND_ENABLE;

  return ret;
}

char *setlocale(int category, const char *locale){
  char * ret = NULL;

  COND_DISABLE;
  INIT_REALFUNCTION;

  ret = Real::setlocale(category, locale);

  COND_ENABLE;

  return ret;
}

int unlink(const char *pathname){
  int ret = -1;

  COND_DISABLE;
  INIT_REALFUNCTION;

  ret = Real::unlink(pathname);

  COND_ENABLE;

  return ret;

}

};
