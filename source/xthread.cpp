/*
 * @file   xthread.cpp
 * @brief  Handle Thread related information.
 */

#include "list.hh"
#include "xthread.hh"

inline int getThreadIndex() {
  return current->index;
}

// This is a really slow procedure and is only called in the pthread_join
// Fortunately, there are not too many alive threads (128 in our setting)
thread_t * xthread::getThread(pthread_t thread) {
  // Search through the active list to find this thread.
  // Holding the global lock to check the thread_t to avoid race.
  thread_t* iterthread;
  thread_t* current = NULL;
  acquireGlobalRLock();

  iterthread = (thread_t*)nextEntry(&_aliveThreadsList);
  while(true) {
    if(iterthread->pthreadt == thread) {
      // Got the thread
      current = iterthread;
      break;
    }

    // if the thread is the tail of the alive list, exit
    if(isListTail(&iterthread->listentry, &_aliveThreadsList) == true) {
      break;
    }

    iterthread = (thread_t *)nextEntry(&iterthread->listentry);
  }	

  releaseGlobalLock();

  return current;
}

