#if !defined(_WATCHPOINT_H)
#define _WATCHPOINT_H

/*
 * @file   watchpoint.h
 * @brief  Watch point handler
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syscall.h>
#include <ucontext.h>
#include <unistd.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <new>
#include <pthread.h>

#include "xdefines.hh"
#include "threadstruct.hh"
#include "selfmap.hh"

class watchpoint {

  public:

    static watchpoint& getInstance() {
      static char buf[sizeof(watchpoint)];
      static watchpoint* theOneTrueObject = new (buf) watchpoint();
      return *theOneTrueObject;
    }

    // Add a watch point with its value to watchpoint list.
    bool setWatchpoint(void* addr, void* objectstart, size_t objectsize, void* callstack, bool ispreempt);
    bool setWatchpointByThread(thread_t* thread);

    // get watchpoint object information 
    watchpointObject* getWatchpointObjectByAddr(void* addr);
    watchpointObject* getWatchpointObjectByFd(int fd);
    watchpointObject* getAllWatchpointObjects();

    // Enable a setof watch points now
    int enable_watchpoint(int fd);
    int disable_watchpoint(int fd);
    bool disableWatchpoint(watchpointObject* object);
    bool disableWatchpointByAddr(void* addr);

    // How many watchpoints that we should care about.
    int getWatchpointsNumber() { return _numWatchpoints; }

    // Handle those traps on watchpoints now.
    static void trapHandler(int sig, siginfo_t* siginfo, void* context);

#ifdef CATCH_SEGV
    static void segvHandler(int sig, siginfo_t* siginfo, void* context);
#endif

  private:
    watchpoint() : _numWatchpoints(0) {
      // init watchpoint info
      for(int i=0; i<xdefines::MAX_WATCHPOINTS; i++){
        // set all watchpoint can be used 
        _wp[i].isUsed = false;
        _wp[i].installtime = 0;
        pthread_spin_init(&(_wp[i].lock), PTHREAD_PROCESS_PRIVATE);
      }

      struct sigaction trap_action;
      // Now we are setting a trap handler for myself.
      trap_action.sa_sigaction = watchpoint::trapHandler;
      trap_action.sa_flags = SA_SIGINFO | SA_RESTART;
      //trap_action.sa_flags = SA_SIGINFO | SA_RESTART | SA_NODEFER;
      sigaction(WP_SIGNAL, &trap_action, NULL);
      //sigaction(SIGIO, &trap_action, NULL);

#ifdef CATCH_SEGV
      struct sigaction segv_action;
      // Now we are setting a segv handler for myself.
      segv_action.sa_sigaction = watchpoint::segvHandler;
      //segv_action.sa_flags = SA_SIGINFO | SA_RESTART;
      segv_action.sa_flags = SA_SIGINFO | SA_RESTART | SA_NODEFER;
      sigaction(SIGSEGV, &segv_action, NULL);
      sigaction(SIGABRT, &segv_action, NULL);
#endif

      curIndex = 0;
    }
    ~watchpoint() {}

    bool setWatchpoint(void* addr, int* fd);
    // Use perf_event_open to install a particular watch points.
    int install_watchpoint(uintptr_t address, pid_t pid, int cpuid, int sig, int group);

    int _numWatchpoints;
    int curIndex;
    // Watchpoint array, we can only support 4 watchpoints totally.
    watchpointObject _wp[xdefines::MAX_WATCHPOINTS];
};

#endif
