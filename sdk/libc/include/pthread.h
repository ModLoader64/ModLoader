#ifndef MODLOADER_LIBC_PTHREAD_H
#define MODLOADER_LIBC_PTHREAD_H

#include <sched.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long pthread_t;
typedef unsigned int pthread_key_t;
typedef int pthread_once_t;
typedef int pthread_spinlock_t;

typedef struct {
    int detachState;
    size_t stackSize;
    size_t guardSize;
} pthread_attr_t;

typedef struct {
    unsigned int state; // 0: free, 1: locked, 2: locked with waiters
    int type;
    unsigned long owner;
    unsigned int count;
} pthread_mutex_t;

typedef struct {
    int type;
    int shared;
} pthread_mutexattr_t;

typedef struct {
    unsigned int sequence;
    clockid_t clock;
} pthread_cond_t;

typedef struct {
    clockid_t clock;
    int shared;
} pthread_condattr_t;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    unsigned int readers;
    unsigned int writer;
} pthread_rwlock_t;

typedef struct {
    int shared;
} pthread_rwlockattr_t;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    unsigned int count;
    unsigned int arrived;
    unsigned int generation;
} pthread_barrier_t;

typedef struct {
    int shared;
} pthread_barrierattr_t;

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT PTHREAD_MUTEX_NORMAL

#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED 1

#define PTHREAD_CANCEL_ENABLE 0
#define PTHREAD_CANCEL_DISABLE 1
#define PTHREAD_CANCEL_DEFERRED 0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1

#define PTHREAD_KEYS_MAX 128
#define PTHREAD_DESTRUCTOR_ITERATIONS 4
#define PTHREAD_STACK_MIN 16384
#define PTHREAD_BARRIER_SERIAL_THREAD (-1)

#define PTHREAD_ONCE_INIT 0
#define PTHREAD_MUTEX_INITIALIZER { 0, PTHREAD_MUTEX_NORMAL, 0, 0 }
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP { 0, PTHREAD_MUTEX_RECURSIVE, 0, 0 }
#define PTHREAD_COND_INITIALIZER { 0, CLOCK_REALTIME }
#define PTHREAD_RWLOCK_INITIALIZER { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0 }

int pthread_attr_init(pthread_attr_t* attributes);
int pthread_attr_destroy(pthread_attr_t* attributes);
int pthread_attr_setdetachstate(pthread_attr_t* attributes, int state);
int pthread_attr_getdetachstate(const pthread_attr_t* attributes, int* out_state);
// Stack/guard sizes are stored for queries
int pthread_attr_setstacksize(pthread_attr_t* attributes, size_t size);
int pthread_attr_getstacksize(const pthread_attr_t* __restrict attributes, size_t* __restrict out_size);
int pthread_attr_setguardsize(pthread_attr_t* attributes, size_t size);
int pthread_attr_getguardsize(const pthread_attr_t* __restrict attributes, size_t* __restrict out_size);

int pthread_create(pthread_t* __restrict out_thread, const pthread_attr_t* __restrict attributes, void* (*routine)(void*), void* __restrict argument);
int pthread_join(pthread_t thread, void** out_result);
int pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
int pthread_equal(pthread_t left, pthread_t right);
__attribute__((noreturn)) void pthread_exit(void* result);
int pthread_setname_np(pthread_t thread, const char* name);
int pthread_getname_np(pthread_t thread, char* out_name, size_t size);
int pthread_setcancelstate(int state, int* out_previous);
int pthread_setcanceltype(int type, int* out_previous);
void pthread_testcancel(void);

int pthread_once(pthread_once_t* once, void (*routine)(void));

int pthread_key_create(pthread_key_t* out_key, void (*destructor)(void*));
int pthread_key_delete(pthread_key_t key);
void* pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void* value);

int pthread_mutexattr_init(pthread_mutexattr_t* attributes);
int pthread_mutexattr_destroy(pthread_mutexattr_t* attributes);
int pthread_mutexattr_settype(pthread_mutexattr_t* attributes, int type);
int pthread_mutexattr_gettype(const pthread_mutexattr_t* __restrict attributes, int* __restrict out_type);
int pthread_mutexattr_setpshared(pthread_mutexattr_t* attributes, int shared);
int pthread_mutexattr_getpshared(const pthread_mutexattr_t* __restrict attributes, int* __restrict out_shared);

int pthread_mutex_init(pthread_mutex_t* __restrict mutex, const pthread_mutexattr_t* __restrict attributes);
int pthread_mutex_destroy(pthread_mutex_t* mutex);
int pthread_mutex_lock(pthread_mutex_t* mutex);
int pthread_mutex_trylock(pthread_mutex_t* mutex);
int pthread_mutex_timedlock(pthread_mutex_t* __restrict mutex, const struct timespec* __restrict deadline); // Absolute CLOCK_REALTIME deadline.
int pthread_mutex_unlock(pthread_mutex_t* mutex);

int pthread_condattr_init(pthread_condattr_t* attributes);
int pthread_condattr_destroy(pthread_condattr_t* attributes);
int pthread_condattr_setclock(pthread_condattr_t* attributes, clockid_t clock);
int pthread_condattr_getclock(const pthread_condattr_t* __restrict attributes, clockid_t* __restrict out_clock);
int pthread_condattr_setpshared(pthread_condattr_t* attributes, int shared);
int pthread_condattr_getpshared(const pthread_condattr_t* __restrict attributes, int* __restrict out_shared);

int pthread_cond_init(pthread_cond_t* __restrict condition, const pthread_condattr_t* __restrict attributes);
int pthread_cond_destroy(pthread_cond_t* condition);
int pthread_cond_wait(pthread_cond_t* __restrict condition, pthread_mutex_t* __restrict mutex);
// Absolute deadline on the condition's clock (CLOCK_REALTIME by default); mutex is reacquired before returning
int pthread_cond_timedwait(pthread_cond_t* __restrict condition, pthread_mutex_t* __restrict mutex, const struct timespec* __restrict deadline);
int pthread_cond_signal(pthread_cond_t* condition);
int pthread_cond_broadcast(pthread_cond_t* condition);

// Reader preferred locks; timed variants use absolute CLOCK_REALTIME deadlines
int pthread_rwlockattr_init(pthread_rwlockattr_t* attributes);
int pthread_rwlockattr_destroy(pthread_rwlockattr_t* attributes);
int pthread_rwlock_init(pthread_rwlock_t* __restrict lock, const pthread_rwlockattr_t* __restrict attributes);
int pthread_rwlock_destroy(pthread_rwlock_t* lock);
int pthread_rwlock_rdlock(pthread_rwlock_t* lock);
int pthread_rwlock_tryrdlock(pthread_rwlock_t* lock);
int pthread_rwlock_timedrdlock(pthread_rwlock_t* __restrict lock, const struct timespec* __restrict deadline);
int pthread_rwlock_wrlock(pthread_rwlock_t* lock);
int pthread_rwlock_trywrlock(pthread_rwlock_t* lock);
int pthread_rwlock_timedwrlock(pthread_rwlock_t* __restrict lock, const struct timespec* __restrict deadline);
int pthread_rwlock_unlock(pthread_rwlock_t* lock);

int pthread_barrierattr_init(pthread_barrierattr_t* attributes);
int pthread_barrierattr_destroy(pthread_barrierattr_t* attributes);
int pthread_barrier_init(pthread_barrier_t* __restrict barrier, const pthread_barrierattr_t* __restrict attributes, unsigned int count);
int pthread_barrier_destroy(pthread_barrier_t* barrier);
int pthread_barrier_wait(pthread_barrier_t* barrier);

int pthread_spin_init(pthread_spinlock_t* lock, int shared);
int pthread_spin_destroy(pthread_spinlock_t* lock);
int pthread_spin_lock(pthread_spinlock_t* lock);
int pthread_spin_trylock(pthread_spinlock_t* lock);
int pthread_spin_unlock(pthread_spinlock_t* lock);

#ifdef __cplusplus
}
#endif

#endif
