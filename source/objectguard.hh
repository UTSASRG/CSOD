#if !defined(__OBJECTHEADER_H)
#define __OBJECTHEADER_H

#include <stddef.h>
#include <stdint.h>

#include "xdefines.hh"

class objectGuard {
  public:
    objectGuard(void* ptr, size_t sz)
      : real_ptr(ptr), objectSize(sz), cs(NULL), head_sentinel(xdefines::SENTINEL_HEAD_WORD) {
        // set tail sentinel
        *getTailSentinel() = xdefines::SENTINEL_TAIL_WORD;
      }

#ifdef STATISTICS
    size_t getObjectSize() { return (size_t)objectSize; }
    void setObjectSize(size_t sz) { objectSize = sz; }
#else
    size_t getObjectSize() { return objectSize; }
    void setObjectSize(size_t sz) { objectSize = sz; }
#endif

    void resetHead() { head_sentinel=0; }
    bool isGoodHead() { return head_sentinel==xdefines::SENTINEL_HEAD_WORD ? true : false; }

    size_t* getTailSentinel() { return (size_t*)((intptr_t) & head_sentinel + objectSize + xdefines::SENTINEL_SIZE); }
    void setTailSentinel() { *getTailSentinel() = xdefines::SENTINEL_TAIL_WORD; }
    bool isGoodTail(){ return *getTailSentinel()==xdefines::SENTINEL_TAIL_WORD ? true : false; }

    void* getStartPtr() { return ((void*)((intptr_t) & head_sentinel + xdefines::SENTINEL_SIZE)); }
    void* getRealPtr() { return real_ptr; }

    void setCallstack(void* ptr) { cs = ptr; }
    void* getCallstack() { return cs; }

#ifdef STATISTICS
    void setIndex(unsigned int idx) { index = idx; }
    unsigned int getIndex() { return index; }
#endif

  private:

    void* real_ptr;
#ifdef STATISTICS
    unsigned int objectSize;
    unsigned int index;
#else
    size_t objectSize;
#endif
    void* cs;
    size_t head_sentinel;
};

inline objectGuard* getObjectGuard(void* ptr) {
  objectGuard* o = (objectGuard*)ptr;
  return (o - 1);
}

inline void* getStartAddr(objectGuard* o) { return (void*)(o + 1); }

#endif
