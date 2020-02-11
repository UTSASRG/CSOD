#if !defined(_XDEFINES_H)
#define _XDEFINES_H

#include <stddef.h>
#include <ucontext.h>
#include <string.h>

/*
 * @file   xdefines.h
 * @brief  Global definitions for Sheriff-Detect and Sheriff-Protect.
 */

#define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)

#ifdef __GNUC__
#	define likely(x)   __builtin_expect(!!(x), 1)
#	define unlikely(x) __builtin_expect(!!(x), 0)
#else
#	define likely(x)   !!(x)
#	define unlikely(x) !!(x)
#endif

extern "C" {

  extern size_t __max_stack_size;

  inline size_t alignup(size_t size, size_t alignto) { return ((size + (alignto - 1)) & ~(alignto - 1)); }
  inline size_t aligndown(size_t addr, size_t alignto) { return (addr & ~(alignto - 1)); }

}; // extern "C"

extern __thread bool isWatching;
inline void enableCauser() { isWatching = true; }
inline void disableCauser() { isWatching = false; }
inline bool isCauser() { return isWatching; }

inline unsigned long getCurrentTime(){
  unsigned long retval = 0;
  struct timespec ts;
  if(clock_gettime(CLOCK_REALTIME, &ts) == 0){
    retval = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
  }
  return retval;
}

extern pthread_rwlock_t  rwlock; 
inline void acquireGlobalRLock() {
  pthread_rwlock_rdlock(&rwlock);
} 

inline void acquireGlobalWLock() {
  pthread_rwlock_wrlock(&rwlock);
} 

inline void releaseGlobalLock() { 
  pthread_rwlock_unlock(&rwlock); 
}

inline unsigned long rdtscp() {
  unsigned int lo, hi;
  asm volatile (
      "rdtscp"
      : "=a"(lo), "=d"(hi) /*  outputs */
      : "a"(0)             /*  inputs */
      : "%rcx");           /*  clobbers*/
      //: "%ebx", "%ecx");   /*  clobbers*/
  unsigned long retval = ((unsigned long)lo) | (((unsigned long)hi) << 32);
  return retval;
}

class xdefines {
  public:

    enum { MAX_ALIVE_THREADS = 1025 };

    enum { MAX_WATCHPOINTS = 4 };
    enum { MAX_CPU_NUM = 32 };
    enum { WP_SEARCH_INDEX_MASK = MAX_WATCHPOINTS - 1 };

    enum { CALLSTACK_MAP_SIZE = 0x80000 };
    enum { MAX_CALLSTACK_SKIP_TOP = 4 };
    enum { MAX_CALLSTACK_SKIP_BOTTOM = 0 };
    //enum { MAX_CALLSTACK_DEPTH = MAX_CALLSTACK_SKIP_TOP + MAX_CALLSTACK_SKIP_BOTTOM + 1 };
    enum { MAX_CALLSTACK_DEPTH = MAX_CALLSTACK_SKIP_TOP + MAX_CALLSTACK_SKIP_BOTTOM + 10 };

    enum { MAX_WATCH_RATIO_UPPERBOUND = 10000 };
    enum { MAX_WATCH_RATIO_SECOND_UPPERBOUND = 100000 };
    enum { MAX_WATCH_THRESHOLD = 5000 };
    enum { MAX_WATCH_PERIOD = 10000 }; //ms
    enum { REDZONESIZE = 1 };

    enum { PAGE_SIZE = 4096UL };
    enum { PAGE_SIZE_MASK = (PAGE_SIZE-1) };

    // reduce percentage after it is watched, actual reduction is WATCHED_REDUCTION / 10 
    enum { WATCHED_REDUCTION = 5 };
    enum { CALLED_REDUCTION = 1 };
    enum { REDUCTION_TO_MIN = 1 };
    enum { INIT_WATCH_RATIO = 5000 };
    enum { WP_INSTALL_MIN_TIME = 1 }; // ms
    enum { WP_PREEMPT_WEIGHT = 2 };
    enum { WP_PREEMPT_TIME_REDUCTION_BASE = 10000 }; // ms
   
    enum { SENTINEL_SIZE = sizeof(size_t) }; 
    enum { SENTINEL_HEAD_WORD = 0xCAFEBABECAFEBABE };
    enum { SENTINEL_TAIL_WORD = 0xDADEBABEDADEBABE };
    enum { SENTINEL_MAGIC_WORD = 0xABEFACECABEFACEC };

    enum { ALLOCATION_MASK = 0xFFFFFFFFFFFFFFF8 };
};

typedef enum {
  MALLOC_OP_CALLED = 0,
  MALLOC_OP_WATCHED
} mallocOpType;

typedef enum {
  CS_NORMAL = 0,
  CS_PHONY
} csType;

typedef struct watchpointsInfo {
  bool isUsed;
  //padding 
  char padding[15];
  unsigned long installtime;
  void* addr;
  void* objectstart;
  size_t objectsize;
  void* callstack;
  pthread_spinlock_t lock;

  int fd[xdefines::MAX_ALIVE_THREADS];
}watchpointObject;

struct callstack {
  int depth;
  int calledCounter;
  int watchedCounter;
  int watchedRatio;
#ifdef STATISTICS
  unsigned long index;
#endif
  //csType type;
  unsigned long period;
  unsigned long periodcalled;
  unsigned long offset;
  size_t hashcode;
  void* stack[xdefines::MAX_CALLSTACK_DEPTH];

  pthread_spinlock_t lock;

  /*  assign operator */
  callstack& operator = (const callstack& cs) {
    if (this != &cs) {
      depth = cs.depth;
      hashcode = cs.hashcode;
      calledCounter = cs.calledCounter;
      watchedCounter = cs.watchedCounter;
      watchedRatio = cs.watchedRatio;
      period = cs.period;
      periodcalled = cs.periodcalled;
      //type = cs.type;
      offset = cs.offset;
#ifdef STATISTICS
      index = cs.index;
#endif
      memcpy(&stack, &cs.stack, xdefines::MAX_CALLSTACK_DEPTH * sizeof(void*));
    }
    return *this;
  }

  bool operator == (const callstack& other) const {
    return stack[0]==other.stack[0] && offset==other.offset;
  }
};

enum {
  DataPages = 2,
  PageSize = 0x1000,
  DataSize = DataPages * PageSize,
  MmapSize = DataSize + PageSize
};

#define LOG_SIZE 4096
#define WP_SIGNAL SIGTRAP
//#define WP_SIGNAL SIGIO

#ifndef PTHREADEXIT_CODE
#define PTHREADEXIT_CODE 2230
#endif

#define INIT_REALFUNCTION         \
  do{                             \
    if(!funcInitialized) {        \
      Real::initializer();        \
      funcInitialized = true;     \
    }                             \
  }while(0)

#define COND_DISABLE              \
  bool watching = isCauser();     \
  do{                             \
    if(watching){                 \
      disableCauser();            \
    }                             \
  }while(0)

#define COND_ENABLE               \
  do{                             \
    if(watching){                 \
      enableCauser();             \
    }                             \
  }while(0)


#define FOR_EACH_THREAD_START(itthread, aliveThreadsList)                  \
  (itthread) = (thread_t*)nextEntry((aliveThreadsList));                   \
  while(true)                                                             

#define FOR_EACH_THREAD_NEXT(itthread, aliveThreadsList)                   \
  if(isListTail(&(itthread)->listentry, (aliveThreadsList)) == true) {     \
    break;                                                                 \
  }                                                                        \
  (itthread) = (thread_t *)nextEntry(&(itthread)->listentry);             

#endif
