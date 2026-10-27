// The newlib of the arduino-pico toolchain (gcc 14.3, earlephilhower) is built with retargetable
// locking and no default locks: arduino-pico brings its own (cores/rp2040/lock.cpp). Its
// sys/lock.h fixes struct __lock at 8 bytes, so a lock here keeps a pointer to a pico-sdk
// recursive mutex from a pool, set up at its first use. These mutexes block a FreeRTOS task
// through the pico_sync interop.

#include <sys/lock.h>
#include "pico/mutex.h"
#include "hardware/sync.h"

struct __lock __lock___arc4random_mutex;
struct __lock __lock___atexit_recursive_mutex;
struct __lock __lock___at_quick_exit_mutex;
struct __lock __lock___env_recursive_mutex;
struct __lock __lock___malloc_recursive_mutex;
struct __lock __lock___sfp_recursive_mutex;
struct __lock __lock___sinit_recursive_mutex;
struct __lock __lock___tz_mutex;
struct __lock __lock___dd_hash_mutex;

#define LOCK_POOL 24
static recursive_mutex_t pool[LOCK_POOL];
static unsigned pool_used;

// lock points at storage of at least a pointer: a struct __lock above, or the _LOCK_T field of
// a FILE (__lock_init passes its address)
static recursive_mutex_t *lock_mutex(_LOCK_T lock) {
  recursive_mutex_t *volatile *slot = (recursive_mutex_t *volatile *) lock;
  recursive_mutex_t *m = *slot;
  if (m) return m;
  spin_lock_t *guard = spin_lock_instance(PICO_SPINLOCK_ID_STRIPED_FIRST);
  uint32_t save = spin_lock_blocking(guard);
  m = *slot;
  if (!m) {
    if (pool_used >= LOCK_POOL) {
      spin_unlock(guard, save);
      panic("newlib: no locks left");
    }
    m = &pool[pool_used++];
    recursive_mutex_init(m);
    *slot = m;
  }
  spin_unlock(guard, save);
  return m;
}

void __retarget_lock_init(_LOCK_T lock) {
  *(recursive_mutex_t **) lock = NULL;
}

void __retarget_lock_init_recursive(_LOCK_T lock) {
  *(recursive_mutex_t **) lock = NULL;
}

// the pool is not given back: files are not opened in the spike
void __retarget_lock_close(_LOCK_T lock) { (void) lock; }
void __retarget_lock_close_recursive(_LOCK_T lock) { (void) lock; }

void __retarget_lock_acquire(_LOCK_T lock) {
  recursive_mutex_enter_blocking(lock_mutex(lock));
}

void __retarget_lock_acquire_recursive(_LOCK_T lock) {
  recursive_mutex_enter_blocking(lock_mutex(lock));
}

int __retarget_lock_try_acquire(_LOCK_T lock) {
  return recursive_mutex_try_enter(lock_mutex(lock), NULL);
}

int __retarget_lock_try_acquire_recursive(_LOCK_T lock) {
  return recursive_mutex_try_enter(lock_mutex(lock), NULL);
}

void __retarget_lock_release(_LOCK_T lock) {
  recursive_mutex_exit(lock_mutex(lock));
}

void __retarget_lock_release_recursive(_LOCK_T lock) {
  recursive_mutex_exit(lock_mutex(lock));
}
