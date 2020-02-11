#if !defined(CAUSER_H)
#define CAUSER_H

/*
 * @file   causer.hh
 */
#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <new>

#include "xdefines.hh"

#include "spinlock.hh"
#include "hashvalue.hh"
#include "hashfuncs.hh"
#include "hashmap.hh"
#include "threadstruct.hh"

#include "watchpoint.hh"

//extern char __executable_start;
//extern char data_start;
 
extern unsigned long causer_stack_offset;

extern float boostratio;
extern __thread thread_t* current;

struct stack_frame {
  struct stack_frame * prev;  // pointing to previous stack_frame
  void * caller_address;    // the address of caller
};

class causer {

  public:

    static causer& getInstance() {
      static char buf[sizeof(causer)];
      static causer* theOneTrueObject = new (buf) causer();
      return *theOneTrueObject;
    }

    void initialize(){ }

    bool startWatch(void* ptr, size_t sz);
    void stopWatch(void* ptr);

    void loadHistoryInfo(char* filename);
    void saveHistoryInfo(char* filename);

#ifdef ENABLE_EVIDENCE
    void* checkPointer(void* addr);
#endif
#ifdef ENABLE_EVIDENCE_SCAN_MEMORY
    void checkAllMemory();
#endif

  private:
    causer() {
      // init global total number
      boostratio = 1;

      causer_stack_offset = 0;

      _csMap.initialize(HashFuncs::hashCallStackT, HashFuncs::compareCallStackT, xdefines::CALLSTACK_MAP_SIZE);
      //_csMap.initialize(HashFuncs::hashSizeT, HashFuncs::compareSizeT, xdefines::CALLSTACK_MAP_SIZE);
      watchpoint::getInstance();
    }
    ~causer() {}

    void updateWatchedInfo(callstack* foundcs, mallocOpType type);

    typedef HashMap<callstack, callstack, spinlock> csHashMap;
    //typedef HashMap<size_t, callstack, spinlock> csHashMap;
    csHashMap _csMap;

};

#endif
