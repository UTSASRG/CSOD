
#include "causer.hh"
#include <dlfcn.h>

#include "selfmap.hh"
#include "objectguard.hh"

extern "C" {
  extern uint32_t arc4random_uniform(uint32_t upper_bound);
  extern uint32_t arc4random(void);
}

#ifdef STATISTICS
extern unsigned int mallocindex;
#endif

#ifdef HAS_LIBUNWIND
#define UNW_LOCAL_ONY
#include <libunwind.h>
static int backtrace(void** stack, int d) {
  unw_cursor_t cursor; unw_context_t uc;
  unw_word_t ip;
  int r = 0;
  unw_getcontext(&uc);
  unw_init_local(&cursor, &uc);
  while ((unw_step(&cursor) > 0) && (d-- > 0)) {
    unw_get_reg(&cursor, UNW_REG_IP, &ip);
    stack[r++] = (void*)ip;
  }
  return r;
}
#else
#include <execinfo.h>
#endif

__attribute__((always_inline)) inline unsigned long getStackOffset(){
  unsigned long esp;
  asm volatile("movq %%rsp,%0\n" 
      : "=r"(esp));
  unsigned long esp_offset = (intptr_t)current->startFrame - esp;
  return esp_offset;
}

//__attribute__ ((noinline)) int getCallsites(void **callsites) {
//__attribute__ ((always_inline)) int getCallsites(void **callsites) {
int getCallsites(void **callsites) {

  void* array[xdefines::MAX_CALLSTACK_DEPTH];
  int frames;
  frames = backtrace(array, xdefines::MAX_CALLSTACK_DEPTH);

  int i = 0;
  int it = 0;
  // skip our library
  while(selfmap::getInstance().isCauserLibrary(array[it]) && it<frames) it++;

  for(; it<frames; it++){
    void * caller_addr = array[it];
    // rule out recursive
    if(it!=0 && caller_addr == array[it-1]) continue;

    callsites[i++] = caller_addr;
  }

  assert(i!=0);

  return i;
}

//__attribute__ ((noinline)) unsigned long getCallSiteKey(void **callsites) {
//__attribute__ ((always_inline)) unsigned long getCallSiteKey(void **callsites) {
unsigned long getCallSiteKey(void **callsites) {

  // Fetch the frame address of the topmost stack frame
  struct stack_frame * current_frame = NULL;

  // Loop condition tests the validity of the frame address given for the
  // previous frame by ensuring it actually points to a location located
  // on the stack
  void * caller_addr = NULL;

  if(causer_stack_offset == 0) {

    current_frame = (struct stack_frame *)(__builtin_frame_address(0));

    // Initialize the prev_frame pointer to equal the current_frame. This
    // simply ensures that the while loop below will be entered and
    // executed and least once
    struct stack_frame * prev_frame = current_frame;

    caller_addr = prev_frame->caller_address;
    while(((void *)prev_frame <= current->startFrame) && 
        (prev_frame >= current_frame) &&
        selfmap::getInstance().isCauserLibrary(caller_addr)
        ) {
      // Walk the prev_frame pointer backward in preparation for the
      // next iteration of the loop
      prev_frame = prev_frame->prev;
      caller_addr = prev_frame->caller_address;
    }
    callsites[0] = caller_addr;

    // save offset
    causer_stack_offset = (intptr_t)prev_frame - (intptr_t)&current_frame;
  } else {
    struct stack_frame * prev_frame = (struct stack_frame *)((intptr_t)&current_frame + causer_stack_offset);
    caller_addr = prev_frame->caller_address;
    callsites[0] = caller_addr;
  }

  return getStackOffset();
}

void causer::updateWatchedInfo(callstack* foundcs, mallocOpType type) {
  pthread_spin_lock(&foundcs->lock);
  // update called number
  foundcs->calledCounter++;
  foundcs->periodcalled++;
  if (type == MALLOC_OP_CALLED){
    if(foundcs->watchedRatio != xdefines::MAX_WATCH_RATIO_UPPERBOUND)
      foundcs->watchedRatio -= xdefines::CALLED_REDUCTION;
  }else if (type == MALLOC_OP_WATCHED){
    // update watched number as well
    foundcs->watchedCounter++;
    if(foundcs->watchedRatio != xdefines::MAX_WATCH_RATIO_UPPERBOUND)
      foundcs->watchedRatio *= xdefines::WATCHED_REDUCTION * 0.1;
  }

  if(foundcs->watchedRatio < xdefines::REDUCTION_TO_MIN){
    foundcs->watchedRatio = xdefines::REDUCTION_TO_MIN;
  }

  // update complete callsite information
  if(foundcs->depth == 0){
    foundcs->depth = getCallsites(foundcs->stack);
  }

  unsigned long now = getCurrentTime();
  if((now-foundcs->period)>xdefines::MAX_WATCH_PERIOD) {
    // || unlikely(now < foundcs->period)) { // FIXME whether time has overflow
    //fprintf(stderr, "reset period %lu, old %lu\n", now, foundcs->period);
    foundcs->periodcalled = 0;
    foundcs->period = now;
  }

  pthread_spin_unlock(&foundcs->lock);
}

// set watchpoint on specific address
bool causer::startWatch(void* ptr, size_t sz){
  callstack curstack;
  curstack.offset = getCallSiteKey(curstack.stack);
  curstack.hashcode = hash_value(curstack.stack[0], (unsigned int)curstack.offset); 

  callstack* foundcs = _csMap.findOrAdd(curstack, sizeof(callstack), curstack);

#ifdef ENABLE_EVIDENCE
  objectGuard* obj = getObjectGuard(ptr);
  obj->setCallstack(foundcs);

#ifdef STATISTICS
  obj->setIndex(__atomic_add_fetch(&mallocindex, 1, __ATOMIC_RELAXED));
  //fprintf(stderr, "malloc %d at %p, sz is %zu, callstack is %p, index is %lu\n", obj->getIndex(), ptr, sz, foundcs, foundcs->index);
#else
  //fprintf(stderr, "malloc at %p, sz is %zu, callstack is %p\n", ptr, sz, foundcs);
#endif
#endif

  void* watchptr = (void*)((intptr_t)ptr+sz);

#ifdef PREEMPT_REPLACEMENT
  int rnd = 0;
  if(foundcs->periodcalled < xdefines::MAX_WATCH_THRESHOLD){
#endif

    /** set watchpoint */
    if(unlikely(watchpoint::getInstance().getWatchpointsNumber() < xdefines::MAX_WATCHPOINTS)){
      if(unlikely(watchpoint::getInstance().setWatchpoint(watchptr, ptr, sz, foundcs, false))){
        updateWatchedInfo(foundcs, MALLOC_OP_WATCHED);
        return true;
      }
    }

#ifdef PREEMPT_REPLACEMENT
    // use arc4random to decide whether we set watchpoint or not
    rnd = arc4random_uniform(xdefines::MAX_WATCH_RATIO_UPPERBOUND);
  } else {
    rnd = arc4random_uniform(xdefines::MAX_WATCH_RATIO_SECOND_UPPERBOUND);
  }

  if(rnd <= foundcs->watchedRatio){
    if(watchpoint::getInstance().setWatchpoint(watchptr, ptr, sz, foundcs, true)){
      updateWatchedInfo(foundcs, MALLOC_OP_WATCHED);
      return true;
    }
#ifndef NDEBUG
    else{
      //fprintf(stderr, "can not grab watchpoint from others, %p\n", foundcs->stack[0]);
    }
#endif
  }
#endif

  updateWatchedInfo(foundcs, MALLOC_OP_CALLED);

  return false;
}

// check whether this address is watched or not
// if not, do nothing, if yes, remove watchpoint 
void causer::stopWatch(void* ptr){
  watchpoint::getInstance().disableWatchpointByAddr(ptr);
}

#ifdef ENABLE_EVIDENCE
void* causer::checkPointer(void* addr){
  objectGuard* obj = getObjectGuard(addr);
  if(obj->isGoodHead()){
    if(!obj->isGoodTail()){
      //fprintf(stderr, "[check at free] Object is overflowed. Tail canary is %zu\n", *obj->getTailSentinel());
      callstack* cs = (callstack *)obj->getCallstack();
      if(cs != NULL){
        pthread_spin_lock(&cs->lock);
        cs->watchedRatio = xdefines::MAX_WATCH_RATIO_UPPERBOUND;
        pthread_spin_unlock(&cs->lock);
#ifdef STATISTICS
        fprintf(stderr, "[check at free] Object %p at callstack %lu is overflowed. Tail canary is %zu\n", addr, cs->index, *obj->getTailSentinel());
#else
        fprintf(stderr, "[check at free] Object %p is overflowed. Tail canary is %zu\n", addr, *obj->getTailSentinel());
#endif
      }
    }
  }else{
    // find previous object
    size_t* prev = (size_t *)obj;
    while(*(prev--) != xdefines::SENTINEL_HEAD_WORD){}

    callstack* cs = *(callstack **)prev; 
    if(cs != NULL){
      pthread_spin_lock(&cs->lock);
      cs->watchedRatio = xdefines::MAX_WATCH_RATIO_UPPERBOUND;
      pthread_spin_unlock(&cs->lock);
    }

    return NULL;
  }

#ifdef ENABLE_EVIDENCE_SCAN_MEMORY
  // avoid double check
  obj->resetHead();
#endif

  return obj->getRealPtr();
}
#endif

#ifdef ENABLE_EVIDENCE_SCAN_MEMORY
void causer::checkAllMemory(){

  fprintf(stderr, "***integrity check in the end***\n");
  ifstream maps_file("/proc/self/maps");
  mapping m;
  size_t *it;
  while(maps_file >> m) {
    //fprintf(stderr, "mapping at %p-%p\n", (void*)m.getBase(), (void*)m.getLimit());
    if(m.isData() && !m.isStack()) {
      it = (size_t*)m.getBase();
      while(it<(size_t*)m.getLimit()){
        if(it && *it==xdefines::SENTINEL_HEAD_WORD){
          objectGuard* obj = getObjectGuard(it+1);
          if(obj->getTailSentinel()>(size_t*)m.getLimit()) break;
          if(!obj->isGoodTail()){
            callstack* cs = (callstack *)obj->getCallstack();
            cs->watchedRatio = xdefines::MAX_WATCH_RATIO_UPPERBOUND;
#ifdef STATISTICS
            fprintf(stderr, "[check in the end] Object %p at callstack %lu is overflowed. Tail canary is %zu\n", (it+1), cs->index, *obj->getTailSentinel());
#else
            fprintf(stderr, "[check in the end] Object %p is overflowed. Tail canary is %zu\n", (it+1), *obj->getTailSentinel());
#endif
          }else{
            // jump to end
            it = obj->getTailSentinel();
            it = (size_t *)((intptr_t)it & xdefines::ALLOCATION_MASK); 
          }
        } 
        it++;
      }
    }
  }
}
#endif

//******* file operation ***************
std::ostream& operator << (std::ostream& os, const callstack& cs) {

  int ratio = cs.watchedRatio;

  if(ratio != xdefines::MAX_WATCH_RATIO_UPPERBOUND){
    if(cs.watchedCounter < 2){
      ratio += (xdefines::MAX_WATCH_RATIO_UPPERBOUND >> 1) * boostratio;
    } else if(cs.watchedCounter < 5){
      ratio += (xdefines::MAX_WATCH_RATIO_UPPERBOUND / (cs.watchedCounter + 1)) * boostratio;
    }

    if (ratio > xdefines::MAX_WATCH_RATIO_UPPERBOUND) {
      ratio = xdefines::MAX_WATCH_RATIO_UPPERBOUND - 1;
    }
  }
  //fprintf(stderr, "original ratio is %d, after boost is %d\n", cs.watchedRatio, ratio);

  os << cs.depth << ' ' << cs.calledCounter << ' ' 
    << cs.watchedCounter << ' ' << ratio << ' ' << cs.offset;
#ifdef STATISTICS
  os << ' ' << cs.index;
#endif
  os << std::endl;

  //Dl_info info;
  if (cs.depth > 0)
    for (int i = 0; i < cs.depth; i++) {
      //if (dladdr(cs.stack[i], &info) && info.dli_saddr != 0) /* saddr can be 0x0 even if returns true */
      mapping m = selfmap::getInstance().getMappingByAddress(cs.stack[i]);
      if(m.valid()){
        os << m.getFile() << ' ' << (uintptr_t)cs.stack[i] - m.getBase();
        //os << info.dli_fname << ' ' << (uintptr_t)cs.stack[i] - (uintptr_t)info.dli_fbase;
      } else {
        os << "_ 0";
      }
      os << ' ' << (uintptr_t)cs.stack[i] << std::endl;
    }
  return os;
}

std::istream& operator >> (std::istream& is, callstack& cs) {
  is >> cs.depth >> cs.calledCounter >> cs.watchedCounter >> cs.watchedRatio >> cs.offset;

#ifdef STATISTICS
  is >> cs.index;
#endif

  //if (cs.depth > 0) {
  std::string s;
  void* s_addr;
  uintptr_t orig_addr;
  size_t off;
  for (int i = 0; i < cs.depth; i++) {
    is >> s >> off >> orig_addr;
    // find base address of library
    mapping m = selfmap::getInstance().getMappingByFileName(s);
    if(m.valid()){
      s_addr = (void*)m.getBase();
    }else{
      s_addr = 0;
    }
    //s_addr = (s == "_") ? 0 : dlopen(s.c_str(), RTLD_LAZY);
    cs.stack[i] = (s_addr) ? (void*)((uintptr_t)s_addr + off) : (void*)orig_addr;
    //fprintf(stderr, "depth %d, addr %s, base %p, %p\n", cs.depth, s.c_str(), s_addr, cs.stack[i]);
  }
  // rehash when load data
  cs.hashcode = hash_value(cs.stack[0], (unsigned int)cs.offset); 
  cs.periodcalled = 0;
  cs.period = getCurrentTime();

  return is;
}

// save history information
void causer::saveHistoryInfo(char* filename){
  fprintf(stderr, "save history file %s, total callsite %zu\n", filename, _csMap.getEntryNumber());
  if(_csMap.getEntryNumber()>0){

    csHashMap::iterator i;
    // compute total malloc number
    for(i=_csMap.begin(); i!=_csMap.end(); i++){
      callstack cs = i.getData();
      if(cs.watchedRatio == xdefines::MAX_WATCH_RATIO_UPPERBOUND){
        boostratio = 0;
        break;
      }
    }

    // save callsite information to file
    std::ofstream ofile;
    ofile.open(filename, std::ofstream::out | std::ofstream::trunc);
    ofile << _csMap.getEntryNumber() << std::endl;
    for(i=_csMap.begin(); i!=_csMap.end(); i++)
      ofile << i.getData();
    ofile.close();
  }
}

void causer::loadHistoryInfo(char* filename) {
  fprintf(stderr, "load history file %s\n", filename);
  std::ifstream ifile;
  ifile.open(filename);
  if (!ifile.is_open())
    return;
  assert(_csMap.getEntryNumber()==0);
  size_t number;
  ifile >> number;

  callstack curstack;
  while(ifile >> curstack){
    // add call stack information to map 
    if(curstack.depth<1){ continue; }
    _csMap.insert(curstack, sizeof(callstack), curstack);
    //_csMap.insert(curstack.hashcode, sizeof(size_t), curstack);
  }
  ifile.close();
}
//******* file operation end ***************
