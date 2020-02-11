#if !defined(_SPINLOCK_H)
#define _SPINLOCK_H

/*
 * @file:   spinlock.h
 * @brief:  spinlock used internally.
 */

#include <pthread.h>

#if 1
class spinlock {
public:
  spinlock() { _lock = 0; }

  void init() { _lock = 0; }

  // Lock
  void lock() {
    while(__atomic_exchange_n(&_lock, 1, __ATOMIC_SEQ_CST) == 1) {
      __asm__("pause");
    }
  }

  void unlock() { __atomic_store_n(&_lock, 0, __ATOMIC_SEQ_CST); }

private:
  int _lock;
};
#else
class spinlock {
public:
  spinlock() { }

  void init() { pthread_spin_init(&_lock, PTHREAD_PROCESS_PRIVATE); }

  // Lock
  void lock() { pthread_spin_lock(&_lock); }

  void unlock() { pthread_spin_unlock(&_lock); }

private:
  pthread_spinlock_t _lock;
};
#endif

#endif /* __SPINLOCK_H__ */
