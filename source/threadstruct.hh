#if !defined(__THREADSTRUCT_H__)
#define __THREADSTRUCT_H__

#include <stddef.h>
#include <ucontext.h>
#include <pthread.h>

#include "list.hh"
#include "xdefines.hh"

typedef void * threadFunction(void *);
typedef struct thread {
  list_t listentry;
  int index;
  // Identifications
  pid_t tid;
  // start frame
  void* startFrame;
  
  // Whether the entry is available so that allocThreadIndex can use this one
  bool available;
  char padding[7];

  pthread_t pthreadt;
  pthread_barrier_t barrier;
  // Starting parameters
  void * startArg;
  threadFunction * startRoutine;
} thread_t;

extern __thread thread_t* current;

#endif
