/*
 * @file   watchpoint.cpp
 */

#include "watchpoint.hh"

#include "xthread.hh"
#include <execinfo.h>
#include <dlfcn.h>
#include <sys/mman.h>

extern "C" {
  extern uint32_t arc4random_uniform(uint32_t upper_bound);
};

long perf_event_open(struct perf_event_attr* hw_event, pid_t pid, int cpu, 
    int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

#define PREV_INSTRUCTION_OFFSET 1
int sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_CALLCHAIN;

// should be protected by lock
// set watchpoint at avaliable place
bool watchpoint::setWatchpoint(void* addr, void* objectstart, size_t objectsize, void* cs, bool ispreempt) {
  //fprintf(stderr, "[try to] set watchpoint at %p, object %p, size %zu\n", addr, objectstart, objectsize);
  bool ret = false;
#ifdef RANDOM_SEARCH_WP
  int sidx = arc4random_uniform(xdefines::MAX_WATCHPOINTS);
#else
  int sidx = curIndex; 
#endif
  for(int i=0; i<xdefines::MAX_WATCHPOINTS && !ret; i++){

    watchpointObject* obj = &_wp[sidx];
    sidx = (sidx + 1) & xdefines::WP_SEARCH_INDEX_MASK;

    if(!obj->isUsed || ispreempt){
      pthread_spin_lock(&obj->lock);

      bool isavalid = false;
      if(!obj->isUsed){
        isavalid = true;
      } else if(ispreempt){
        callstack* installed = (callstack*) obj->callstack;
        callstack* current = (callstack*) cs;
        //unsigned long now = rdtscp();
        unsigned long now = getCurrentTime();
        unsigned long difftime = now - obj->installtime;
        //fprintf(stderr, "%p, difftime %lu, current ratio %d, installed ratio %d, %f\n", objectstart, difftime, current->watchedRatio, installed->watchedRatio, (installed->watchedRatio * xdefines::WP_PREEMPT_WEIGHT * (1 - difftime * 1.0 / xdefines::WP_PREEMPT_TIME_REDUCTION_BASE)));
        if(difftime >= xdefines::WP_INSTALL_MIN_TIME
            && current->watchedRatio > 
            (installed->watchedRatio * xdefines::WP_PREEMPT_WEIGHT * (1 - difftime * 1.0 / xdefines::WP_PREEMPT_TIME_REDUCTION_BASE))){
          //(installed->watchedRatio * xdefines::WP_PREEMPT_WEIGHT > difftime / xdefines::WP_PREEMPT_TIME_REDUCTION_BASE ? 
          // installed->watchedRatio * xdefines::WP_PREEMPT_WEIGHT - difftime / xdefines::WP_PREEMPT_TIME_REDUCTION_BASE : 0))
          isavalid = true;
        }
      }

      if(isavalid){
        // acquire global lock, this lock protects alive thread list
        acquireGlobalRLock();

        //fprintf(stderr, "set watchpoint %d at %p, object %p, size %zu\n", sidx?sidx-1:3, addr, objectstart, objectsize);

        list_t* aliveThreadsList = xthread::getInstance().getAliveThreadsList();

        thread_t* iterthread = NULL;
        if(obj->isUsed){
          // disable current watchpoint
          FOR_EACH_THREAD_START(iterthread, aliveThreadsList) {
            disable_watchpoint(obj->fd[iterthread->index]);
            obj->fd[iterthread->index] = -1;
            FOR_EACH_THREAD_NEXT(iterthread, aliveThreadsList)
          }
        }

        obj->isUsed = true;
        //set values
        obj->addr = addr;
        obj->objectstart = objectstart;
        obj->objectsize = objectsize;
        obj->callstack = cs;

        //install wachpoint
        ret = setWatchpoint(addr, obj->fd);
        if(ret){
          // installed time 
          //obj->installtime = rdtscp();
          obj->installtime = getCurrentTime();
          __atomic_store(&curIndex, &sidx, __ATOMIC_RELAXED);
        }else{
          obj->isUsed = false;
        }

        // release global lock
        releaseGlobalLock();
      }

      pthread_spin_unlock(&obj->lock);
      
    }
  }

  return ret;
}

bool watchpoint::setWatchpointByThread(thread_t* thread){
  bool ret = true;
  for(int i=0; i<xdefines::MAX_WATCHPOINTS; i++){
    watchpointObject* obj = &_wp[i];

    if(obj->isUsed){
      obj->fd[thread->index] = install_watchpoint((uintptr_t)(obj->addr), thread->tid, -1, WP_SIGNAL, -1); 

      if(obj->fd[thread->index] == -1
          || enable_watchpoint(obj->fd[thread->index]) == -1){
        ret = false;
        break;
      }
    }
  }
  return ret;
}

bool watchpoint::setWatchpoint(void* addr, int* fd) {
  // FIXME test
  //__atomic_add_fetch(&_numWatchpoints, 1, __ATOMIC_RELAXED);
  //return true;

  bool ret = true;

  int installednum = 0;
  thread_t* iterthread = NULL;
  list_t* aliveThreadsList = xthread::getInstance().getAliveThreadsList();

  FOR_EACH_THREAD_START(iterthread, aliveThreadsList) {
    // install this watch point.
    fd[iterthread->index] = install_watchpoint((uintptr_t)addr, iterthread->tid, -1, WP_SIGNAL, -1); 

    //Now we can start those watchpoints.
    if(fd[iterthread->index] == -1
        || enable_watchpoint(fd[iterthread->index]) == -1){
      ret = false;
      break;
    } 

    installednum++;

    FOR_EACH_THREAD_NEXT(iterthread, aliveThreadsList)
  }

  if(ret){
    __atomic_add_fetch(&_numWatchpoints, 1, __ATOMIC_RELAXED);
  }else{ 
    // rollback enabled watchpoint
    FOR_EACH_THREAD_START(iterthread, aliveThreadsList) {
      if(fd[iterthread->index] != -1){
        disable_watchpoint(fd[iterthread->index]);
        fd[iterthread->index] = -1;
      }
      if(--installednum < 0){
        break;
      }
      FOR_EACH_THREAD_NEXT(iterthread, aliveThreadsList)
    }
  }

  return ret;
}

int watchpoint::install_watchpoint(uintptr_t address, pid_t pid, int cpuid, int sig, int group) {
  // Perf event settings
  struct perf_event_attr pe;

  memset(&pe, 0, sizeof(pe));
  pe.type = PERF_TYPE_BREAKPOINT;
  pe.size = sizeof(pe);
  pe.bp_type = HW_BREAKPOINT_RW;
  pe.bp_len = HW_BREAKPOINT_LEN_1;
  pe.bp_addr = (uintptr_t)address;
  pe.disabled = 1;
  pe.sample_period = 1;
  //pe.sample_type = sample_type;

  int perf_fd = -1;
  // Create the perf_event for this thread on all CPUs with no event group, use pid instead of 0. 
  perf_fd = perf_event_open(&pe, pid, cpuid, group, 0);
  if(perf_fd == -1) {
    fprintf(stderr, "Failed to open perf event file: pid %d,  %s\n", pid, strerror(errno));
    return -1;
  }

  // Set the perf_event file to async mode
  if(fcntl(perf_fd, F_SETFL, fcntl(perf_fd, F_GETFL, 0) | O_ASYNC) == -1) {
    fprintf(stderr, "Failed to set perf event file to ASYNC mode: %s\n", strerror(errno));
    return -1;
  }

  // Tell the file to send a SIGTRAP when an event occurs
  if(fcntl(perf_fd, F_SETSIG, sig) == -1) {
    fprintf(stderr, "Failed to set perf event file's async signal: %s\n", strerror(errno));
    return -1;
  }

  // Deliver the signal to current thread
  struct f_owner_ex owner = {F_OWNER_TID, pid};
  if(fcntl(perf_fd, F_SETOWN_EX, &owner) == -1) {
    fprintf(stderr, "Failed to set the owner of the perf event file: %s\n", strerror(errno));
    return -1;
  }
  return perf_fd;
}

int watchpoint::enable_watchpoint(int fd) {
  //return 0;
  int ret;
  // Start the event
  if((ret = ioctl(fd, PERF_EVENT_IOC_ENABLE, 0)) == -1) {
    fprintf(stderr, "Failed to enable perf event: %s\n", strerror(errno));
  }

  return ret;
}

int watchpoint::disable_watchpoint(int fd) {
  //return 0;
  int ret = 0;
  if(fd < 3){
    return -1;
  }

  // we should close fd, otherwise it is still occupied
  ret -= ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
  ret -= close(fd);

  if(ret < 0) {
    fprintf(stderr, "Failed to disable perf event: %s\n", strerror(errno));
  }

  return ret;
}

// this function should be protected by globallock and per-watchpoint spinlock
bool watchpoint::disableWatchpoint(watchpointObject* object){ 

  bool ret = true;

  if(object != NULL){
    list_t* aliveThreadsList = xthread::getInstance().getAliveThreadsList();
    thread_t* iterthread = NULL;
    FOR_EACH_THREAD_START(iterthread, aliveThreadsList) {
      ret &= !(disable_watchpoint(object->fd[iterthread->index]) < 0); 
      object->fd[iterthread->index] = -1;
      FOR_EACH_THREAD_NEXT(iterthread, aliveThreadsList)
    }

    if(ret) {
      __atomic_sub_fetch(&_numWatchpoints, 1, __ATOMIC_RELAXED);
      object->isUsed = false;
    }
  }

  return ret;
}

// since we set watchpoint at all cpus, we should disable all of them
bool watchpoint::disableWatchpointByAddr(void* addr){ 

  bool ret = true;
  watchpointObject* object = getWatchpointObjectByAddr(addr);

  if(object != NULL){
    pthread_spin_lock(&object->lock);
    if (object->isUsed && addr == object->objectstart){
      acquireGlobalRLock();
      disableWatchpoint(object);
      releaseGlobalLock();
    }
    pthread_spin_unlock(&object->lock);
  }

  return ret;
}

watchpointObject* watchpoint::getWatchpointObjectByFd(int fd) {
  watchpointObject* obj = NULL;
  thread_t* iterthread = NULL;
  list_t* aliveThreadsList = xthread::getInstance().getAliveThreadsList();
  for(int i = 0; i < xdefines::MAX_WATCHPOINTS; i++) {
    obj = &_wp[i];
    if(obj->isUsed){
      FOR_EACH_THREAD_START(iterthread, aliveThreadsList) {
        if(obj->fd[iterthread->index] == fd){
          return obj;
        }
        FOR_EACH_THREAD_NEXT(iterthread, aliveThreadsList)
      }
    }
  }
  return NULL;
}

// get watchpoint by startaddress
watchpointObject* watchpoint::getWatchpointObjectByAddr(void* addr) {
  watchpointObject* obj = NULL;
  for(int i = 0; i < xdefines::MAX_WATCHPOINTS; i++) {
    if(_wp[i].isUsed && _wp[i].objectstart == addr) {
      obj = &_wp[i];
      break;
    }
  }

  return obj; 
}

// get all watchpoint object
watchpointObject* watchpoint::getAllWatchpointObjects() {
  return _wp;
}

/* **************************** perf signal handler end ******************************* */

#ifdef CUSTOMIZED_REPORT
std::string exec(const char* cmd) {
  FILE* pipe = popen(cmd, "r");
  if (!pipe) return "ERROR";
  char buffer[256];
  std::string result = "";
  while (!feof(pipe)) {
    if (fgets(buffer, 256, pipe) != NULL)
      result += buffer;
  }
  pclose(pipe);
  return result;
}

std::string fetch_line(std::string libname, void *ptr){
  std::string source_line = "";
  char buf[4096];
  sprintf(buf, "addr2line -e %s -a %p | tail -n +2", libname.c_str(), ptr);
  source_line = exec(buf);
  return source_line;
}
#endif

void printLineOfCode(void *ptr) {
#ifndef CUSTOMIZED_REPORT
  char buf[256];
#endif
  if(selfmap::getInstance().isApplication(ptr)){
    void* addr = (void*)((uintptr_t)ptr - PREV_INSTRUCTION_OFFSET);
#ifdef CUSTOMIZED_REPORT
    fprintf(stderr, "%s\n", fetch_line(selfmap::getInstance().getMainNameString(), addr).c_str());
#else
    sprintf(buf, "addr2line -a -i -e %s %p", selfmap::getInstance().getMainName(), addr);
    //sprintf(buf, "addr2line -e %s -a %p | tail -1", selfmap::getInstance().getMainName(), addr);
    system(buf);
#endif
  } else {
    mapping m = selfmap::getInstance().getMappingByAddress(ptr);
    if(m.valid()){
      void* addr = (void*)((uintptr_t)ptr - m.getBase() - PREV_INSTRUCTION_OFFSET);
#ifdef CUSTOMIZED_REPORT
      fprintf(stderr, "%s\n", fetch_line(m.getFile(), addr).c_str());
#else
      sprintf(buf, "addr2line -a -i -e %s %p", m.getFile().c_str(), addr);
      //sprintf(buf, "addr2line -e %s -a %p | tail -1", m.getFile().c_str(), addr);
      system(buf);
#endif
    }
  }
}

bool checkGlibcWL(void* itptr, unsigned long offset, Dl_info &info){
  bool benignBF = false;
  std::string fname(info.dli_fname);
  if(
      // strcmp
      (offset>=0x13f5c9 && offset<=0x141434) ||
      (offset>=0x89cce && offset<=0x8bb70) ||
      (offset>=0x86e07 && offset<=0x87f38) ||
      // strlen
      (offset>=0x88a7f && offset<=0x88dfc) ||
      // strcmp-sse2 
      (offset>=0x9fcbe && offset<=0x9fcf5) ||
      // strcmp-sse2-unaligned
      (offset>=0x9fcfa && offset<=0x9feac) ||
      // strcmp-sse42
      (offset>=0x145310 && offset<=0x14a467) ||
      // strchr 
      (offset>=0x89a77 && offset<=0x93c24) ||
      // strstr-sse2-unaligned
      (offset>=0xa1211 && offset<=0xa149f) ||
      // strstr-sse2
      (offset>=0xa9201 && offset<=0xa922c) ||
      // strcat
      (offset == 0xa7948) ||
      // strcat-sse2-unaligned
      (offset>=0xa79cd && offset<=0xa79f3) ||
      // strcpy
      (offset>=0xa67a0 && offset<=0xa69a0) ||
      // _IO_vfprintf
      (offset == 0x4e4b4) ||
      // __lxstat
      (offset == 0xf6eb5)
      ){
        //fprintf(stderr, "***inside the trap handler, it is in white-list, %p, base %p, offset %ld\n", itptr, info.dli_fbase, offset);
        benignBF = true;
        goto gotoEnd;
      } 
  else if(info.dli_sname != NULL){
    std::string sname(info.dli_sname);
    if(//sname.find("strlen") != std::string::npos || 
        sname.find("strrchr") != std::string::npos
        || sname.find("memchr") != std::string::npos
        || sname.find("xstat64") != std::string::npos
        ) {
      //fprintf(stderr, "***inside the trap handler, triggered by strlen, strrchr, memchr or xstat64, %p, base %p, offset %ld\n", itptr, info.dli_fbase, offset);
      benignBF = true;
      goto gotoEnd;
    }
  }
  else if(fname.find("ld-linux-") != std::string::npos) {
    //fprintf(stderr, "***inside the trap handler, triggered by libld, %p, base %p, offset %ld\n", itptr, info.dli_fbase, offset);
    benignBF = true;
    goto gotoEnd;
  } 


gotoEnd:
  return benignBF;
}

// Handle those traps on watchpoints now.
void watchpoint::trapHandler(int /* sig */, siginfo_t* siginfo, void* context) {
  int fd = siginfo->si_fd; // fd

  //return;

  if(!isCauser()) return;

  // disable watcher
  COND_DISABLE;

  bool benignBF = false;

  void* array[256];
  void* itptr = NULL;
  int it = 0, frames = 0;

  ucontext_t* trapcontext = (ucontext_t*)context;
  size_t* insaddr = (size_t*)trapcontext->uc_mcontext.gregs[REG_RIP]; // address of access

  Dl_info info;
  if(selfmap::getInstance().isLibcLibrary(insaddr)) {
    unsigned long offset = 0;
    dladdr(insaddr, &info);
    offset = (intptr_t)insaddr-(intptr_t)info.dli_fbase;
    if(offset!=0x352f0){
      benignBF = checkGlibcWL(insaddr, offset, info);
    }
  }

  if(!benignBF){
    /* check whether overflow is benigned  */
    frames = backtrace(array, 256);
    while(selfmap::getInstance().isCauserLibrary(itptr = array[it++])){ }

    if(selfmap::getInstance().isPthreadLibrary(itptr)) {
      itptr = array[it++];
      //benignBF = true;
    }
    /*
    */
    //Dl_info info;
    if(selfmap::getInstance().isLibcLibrary(itptr)) {
      unsigned long offset = 0;
GetPtr:
      dladdr(itptr, &info);
      offset = (intptr_t)itptr-(intptr_t)info.dli_fbase;
      if(offset==0x352f0){
        itptr = array[it++];
        goto GetPtr;
      }
      benignBF = checkGlibcWL(itptr, offset, info);
    } else {
      dladdr(itptr, &info);
      std::string fname(info.dli_fname);
      if(fname.find("ld-linux-") != std::string::npos) {
        //fprintf(stderr, "***inside the trap handler, triggered by libld, %p, base %p\n", itptr, info.dli_fbase);
        benignBF = true;
      }
    }
  }

  /* report overflow information */
  if(!benignBF){

    acquireGlobalRLock();
    fprintf(stderr, "***inside the trap handler, fd %d\n", fd);
    watchpointObject* wpObj = (watchpointObject*)watchpoint::getInstance().getWatchpointObjectByFd(fd);

    if (wpObj != NULL){
#ifdef ENABLE_DLADDR_INFO
      Dl_info info;
#endif
      bool isread = false;

      ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
      size_t* installedaddr = (size_t*)wpObj->addr;
      if(*installedaddr == xdefines::SENTINEL_TAIL_WORD){
        isread = true;
      }
      ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);

      if(isread){
        fprintf(stderr, "A buffer over-read problem is detected at:\n");
      }else{
        fprintf(stderr, "A buffer over-write problem is detected at:\n");
      }

      //void* array[256];
      //int frames = backtrace(array, 256);
      for(it--; it < frames; it++) {

        if(!selfmap::getInstance().isCauserLibrary(array[it])){

#ifdef ENABLE_DLADDR_INFO
          dladdr(array[it], &info);
          fprintf(stderr, "ip %p, dli_fname %s, dli_fbase %p, dli_sname %s\n", array[it], info.dli_fname, info.dli_fbase, info.dli_sname);
#endif
          printLineOfCode(array[it]);
        }
      }

      fprintf(stderr, "This object is allocated at:\n");
      callstack* cs = (callstack*)wpObj->callstack;
      void** callsite = cs->stack;
      for(int i=0; i<cs->depth; i++){

#ifdef ENABLE_DLADDR_INFO
        dladdr(callsite[i], &info);
        fprintf(stderr, "ip %p, dli_fname %s, dli_fbase %p, dli_sname %s\n", callsite[i], info.dli_fname, info.dli_fbase, info.dli_sname);
#endif
        printLineOfCode(callsite[i]);
      }

    }

    releaseGlobalLock();

  }

  //exit(0);
  COND_ENABLE;
}

/* **************************** perf signal handler end ******************************* */

/* **************************** SEGV signal handler ******************************* */
#ifdef CATCH_SEGV
void watchpoint::segvHandler(int sig, siginfo_t* siginfo, void* context) {
  ucontext_t* segvcontext = (ucontext_t*)context;
  size_t* ip = (size_t*)segvcontext->uc_mcontext.gregs[REG_RIP]; 
  void* memaddr = siginfo->si_addr; // address of access

  COND_DISABLE;

  fprintf(stderr, "***Crash site: ip %p tries to access  memory address %p\n", ip, memaddr);

  char buf[256];
  void* array[256];
  int frames = backtrace(array, 256);
  for(int i = 0; i < frames; i++) {
    if(selfmap::getInstance().isApplication(array[i])){
      void* addr = (void*)((unsigned long)array[i] - PREV_INSTRUCTION_OFFSET);
      sprintf(buf, "addr2line -a -i -e %s %p", selfmap::getInstance().getMainName(), addr);
      system(buf);
    }
  }

  COND_ENABLE;
  exit(0);
}
#endif
/* **************************** SEGV signal handler end ******************************* */
