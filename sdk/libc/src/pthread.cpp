#include "internal.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern "C" {

uint64_t ModLoader_Host_Thread_Prepare() asm("modloader_thread_prepare");
void ModLoader_Host_Thread_Adopt(uint64_t context) asm("modloader_thread_adopt");
void ModLoader_Host_Thread_Release(uint64_t context) asm("modloader_thread_release");
void ModLoader_Host_Thread_Start(uint64_t context) asm("modloader_thread_start");

// wasm-ld builds this for shared memory: it copies the thread-local image into a block and points __tls_base at it
void __wasm_init_tls(void* block);

} // extern "C"

namespace {

constexpr int64_t gWaitForever = -1;
constexpr uint32_t gWakeAll = 0xFFFFFFFFu;

struct Thread_Record {
    void* (*routine)(void*);
    void* argument;
    void* result;
    void* tls;
    uint32_t hostId; // set once thread_spawn returns (0 before)
    uint32_t finished; // the routine returned and the thread cleaned up
    uint32_t detached;
    Thread_Record* nextDetached;
};

// The thread the host calls the module on has no record of its own; this stands in for it
Thread_Record sMainThread;
thread_local Thread_Record* sSelf;

// Detached threads, joined with the host (and freed) once they finish
Thread_Record* sDetached;
Libc::Spin_Lock sDetachedLock;

// __cxa_thread_atexit_impl's handlers for this thread (destructors of thread_local objects), newest first
struct Thread_Exit_Handler {
    void (*function)(void*);
    void* object;
    Thread_Exit_Handler* next;
};

thread_local Thread_Exit_Handler* sThreadExitHandlers;

// Keys: a slot's generation changes when the key is deleted, so a stale value from an old key reads as null
struct Key_Slot {
    bool used;
    uint32_t generation;
    void (*destructor)(void*);
};

struct Key_Value {
    uint32_t generation;
    void* value;
};

Key_Slot sKeys[PTHREAD_KEYS_MAX];
Libc::Spin_Lock sKeyLock;
thread_local Key_Value sKeyValues[PTHREAD_KEYS_MAX];

constexpr uint32_t gKeyIndexBits = 8;
constexpr uint32_t gKeyIndexMask = (1u << gKeyIndexBits) - 1;

int Wait(uint32_t* address, uint32_t expected, int64_t timeout_nanoseconds) {
    return __builtin_wasm_memory_atomic_wait32(reinterpret_cast<int*>(address), static_cast<int>(expected), timeout_nanoseconds);
}

void Wake(uint32_t* address, uint32_t count) {
    __builtin_wasm_memory_atomic_notify(reinterpret_cast<int*>(address), count);
}

Thread_Record* Self() {
    return sSelf != nullptr ? sSelf : &sMainThread;
}

// Nanoseconds from now until deadline on clock; 0 or less when it passed
int64_t Remaining(const timespec* deadline, clockid_t clock) {
    timespec now;

    clock_gettime(clock, &now);
    return (deadline->tv_sec - now.tv_sec) * 1000000000LL + (deadline->tv_nsec - now.tv_nsec);
}

// Joins and frees the detached threads that finished
void Reap_Detached() {
    Thread_Record* finished = nullptr;

    {
        Libc::Scoped lock(sDetachedLock);
        Thread_Record** link = &sDetached;
        while (*link != nullptr) {
            Thread_Record* thread = *link;

            if (__atomic_load_n(&thread->finished, __ATOMIC_ACQUIRE) != 0) {
                *link = thread->nextDetached;
                thread->nextDetached = finished;
                finished = thread;
            }
            else {
                link = &thread->nextDetached;
            }
        }
    }

    while (finished != nullptr) {
        Thread_Record* next = finished->nextDetached;

        // A thread that detached itself may finish before its creator stored the host ID
        while (__atomic_load_n(&finished->hostId, __ATOMIC_ACQUIRE) == 0) {
            Wait(&finished->hostId, 0, gWaitForever);
        }
        ModLoader_Host_Thread_Join(finished->hostId);
        free(finished);
        finished = next;
    }
}

void Run_Thread_Exit_Handlers() {
    while (sThreadExitHandlers != nullptr) {
        Thread_Exit_Handler* handler = sThreadExitHandlers;

        sThreadExitHandlers = handler->next;
        handler->function(handler->object);
        free(handler);
    }
}

void Run_Key_Destructors() {
    for (int round = 0; round < PTHREAD_DESTRUCTOR_ITERATIONS; round++) {
        bool ran_any = false;

        for (uint32_t index = 0; index < PTHREAD_KEYS_MAX; index++) {
            Key_Value* value = &sKeyValues[index];
            void* data;

            if (value->value == nullptr) {
                continue;
            }

            void (*destructor)(void*) = nullptr;
            {
                Libc::Scoped lock(sKeyLock);
                if (sKeys[index].used && sKeys[index].generation == value->generation) {
                    destructor = sKeys[index].destructor;
                }
            }

            data = value->value;
            value->value = nullptr;
            if (destructor != nullptr) {
                destructor(data);
                ran_any = true;
            }
        }

        if (!ran_any) {
            return;
        }
    }
}

int Mutex_Lock(pthread_mutex_t* mutex, const timespec* deadline) {
    unsigned long self = reinterpret_cast<unsigned long>(Self());
    uint32_t expected = Libc::Mutex_State::Unlocked;

    if (mutex->type != PTHREAD_MUTEX_NORMAL && __atomic_load_n(&mutex->owner, __ATOMIC_RELAXED) == self) {
        if (mutex->type == PTHREAD_MUTEX_ERRORCHECK) {
            return EDEADLK;
        }
        if (mutex->count == 0xFFFFFFFFu) {
            return EAGAIN;
        }
        mutex->count++;
        return 0;
    }
    if (!__atomic_compare_exchange_n(&mutex->state, &expected, Libc::Mutex_State::Locked, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        // Mark contention so the holder wakes someone, then sleep until it is free
        if (expected != Libc::Mutex_State::Contended) {
            expected = __atomic_exchange_n(&mutex->state, Libc::Mutex_State::Contended, __ATOMIC_ACQUIRE);
        }
        while (expected != Libc::Mutex_State::Unlocked) {
            int64_t timeout = gWaitForever;

            if (deadline != nullptr) {
                timeout = Remaining(deadline, CLOCK_REALTIME);
                if (timeout <= 0) {
                    return ETIMEDOUT;
                }
            }

            Wait(&mutex->state, Libc::Mutex_State::Contended, timeout);
            expected = __atomic_exchange_n(&mutex->state, Libc::Mutex_State::Contended, __ATOMIC_ACQUIRE);
        }
    }
    __atomic_store_n(&mutex->owner, self, __ATOMIC_RELAXED);
    mutex->count = 1;
    return 0;
}

int Condition_Wait(pthread_cond_t* condition, pthread_mutex_t* mutex, const timespec* deadline) {
    uint32_t sequence = __atomic_load_n(&condition->sequence, __ATOMIC_ACQUIRE);
    int result = 0;
    int64_t timeout;

    pthread_mutex_unlock(mutex);
    timeout = gWaitForever;
    if (deadline != nullptr) {
        timeout = Remaining(deadline, condition->clock);
        if (timeout <= 0) {
            result = ETIMEDOUT;
        }
    }

    if (result == 0 && Wait(&condition->sequence, sequence, timeout) == 2) {
        result = ETIMEDOUT;
    }
    Mutex_Lock(mutex, nullptr);
    return result;
}

int Read_Lock(pthread_rwlock_t* lock, const timespec* deadline) {
    Mutex_Lock(&lock->lock, nullptr);
    while (lock->writer != 0) {
        if (Condition_Wait(&lock->changed, &lock->lock, deadline) == ETIMEDOUT && lock->writer != 0) {
            pthread_mutex_unlock(&lock->lock);
            return ETIMEDOUT;
        }
    }
    lock->readers++;
    pthread_mutex_unlock(&lock->lock);
    return 0;
}

int Write_Lock(pthread_rwlock_t* lock, const timespec* deadline) {
    Mutex_Lock(&lock->lock, nullptr);
    while (lock->writer != 0 || lock->readers != 0) {
        if (Condition_Wait(&lock->changed, &lock->lock, deadline) == ETIMEDOUT && (lock->writer != 0 || lock->readers != 0)) {
            pthread_mutex_unlock(&lock->lock);
            return ETIMEDOUT;
        }
    }
    lock->writer = 1;
    pthread_mutex_unlock(&lock->lock);
    return 0;
}

// A new thread's thread-local storage, from this thread's malloc; false when memory ran out
bool Allocate_Tls(Thread_Record* thread) {
    size_t size = __builtin_wasm_tls_size();

    if (size == 0) {
        return true;
    }
    thread->tls = aligned_alloc(__builtin_wasm_tls_align(), size);
    return thread->tls != nullptr;
}

} // namespace

extern "C" {

// threads

// A thread not made by pthread_create (the UI thread): the main thread prepares its record and TLS, the instance adopts them
LIBC_EXPORT("modloader_thread_prepare") uint64_t ModLoader_Host_Thread_Prepare() {
    Thread_Record* thread = static_cast<Thread_Record*>(calloc(1, sizeof(Thread_Record)));

    if (thread == nullptr || !Allocate_Tls(thread)) {
        free(thread);
        return 0;
    }
    return reinterpret_cast<uint64_t>(thread);
}

LIBC_EXPORT("modloader_thread_adopt") void ModLoader_Host_Thread_Adopt(uint64_t context) {
    Thread_Record* thread = reinterpret_cast<Thread_Record*>(context);

    if (thread->tls != nullptr) {
        __wasm_init_tls(thread->tls);
    }
    sSelf = thread;
}

LIBC_EXPORT("modloader_thread_release") void ModLoader_Host_Thread_Release(uint64_t context) {
    Thread_Record* thread = reinterpret_cast<Thread_Record*>(context);
    if (thread == nullptr) {
        return;
    }
    if (sSelf == thread) {
        Run_Thread_Exit_Handlers();
        Run_Key_Destructors();
        sSelf = nullptr;
    }
    free(thread->tls);
    free(thread);
}

LIBC_EXPORT("modloader_thread_start") void ModLoader_Host_Thread_Start(uint64_t context) {
    Thread_Record* thread = reinterpret_cast<Thread_Record*>(context);
    void* result;

    if (thread->tls != nullptr) {
        __wasm_init_tls(thread->tls);
    }

    sSelf = thread;
    result = thread->routine(thread->argument);
    Run_Thread_Exit_Handlers();
    Run_Key_Destructors();
    thread->result = result;
    // Nothing reads thread local memory past this point
    free(thread->tls);
    __atomic_store_n(&thread->finished, 1, __ATOMIC_RELEASE);
}

int pthread_create(pthread_t* __restrict out_thread, const pthread_attr_t* __restrict attributes, void* (*routine)(void*), void* __restrict argument) {
    Thread_Record* thread;
    bool detached;
    uint32_t id;

    Reap_Detached();
    thread = static_cast<Thread_Record*>(calloc(1, sizeof(Thread_Record)));
    if (thread == nullptr) {
        return EAGAIN;
    }

    thread->routine = routine;
    thread->argument = argument;
    if (!Allocate_Tls(thread)) {
        free(thread);
        return EAGAIN;
    }

    detached = attributes != nullptr && attributes->detachState == PTHREAD_CREATE_DETACHED;
    if (detached) {
        thread->detached = 1;
        Libc::Scoped lock(sDetachedLock);
        thread->nextDetached = sDetached;
        sDetached = thread;
    }

    id = ModLoader_Host_Thread_Spawn(reinterpret_cast<uint64_t>(thread));
    if (id == 0) {
        if (detached) {
            Thread_Record** link;

            Libc::Scoped lock(sDetachedLock);

            link = &sDetached;
            while (*link != thread) {
                link = &(*link)->nextDetached;
            }
            *link = thread->nextDetached;
        }
        free(thread->tls);
        free(thread);
        return EAGAIN;
    }
    __atomic_store_n(&thread->hostId, id, __ATOMIC_RELEASE);
    Wake(&thread->hostId, gWakeAll);
    *out_thread = reinterpret_cast<pthread_t>(thread);
    return 0;
}

int pthread_join(pthread_t handle, void** out_result) {
    Thread_Record* thread = reinterpret_cast<Thread_Record*>(handle);

    if (thread == Self()) {
        return EDEADLK;
    }

    if (thread == &sMainThread || __atomic_load_n(&thread->detached, __ATOMIC_ACQUIRE) != 0) {
        return EINVAL;
    }
    
    ModLoader_Host_Thread_Join(thread->hostId);
    if (out_result != nullptr) {
        *out_result = thread->result;
    }
    free(thread);
    return 0;
}

int pthread_detach(pthread_t handle) {
    Thread_Record* thread = reinterpret_cast<Thread_Record*>(handle);

    if (thread == &sMainThread) {
        return EINVAL;
    }
    {
        Libc::Scoped lock(sDetachedLock);
        if (thread->detached != 0) {
            return EINVAL;
        }
        __atomic_store_n(&thread->detached, 1, __ATOMIC_RELEASE);
        thread->nextDetached = sDetached;
        sDetached = thread;
    }
    // A thread detaching itself cannot join itself; the next pthread_create or pthread_detach reaps it
    if (thread != Self()) {
        Reap_Detached();
    }
    return 0;
}

pthread_t pthread_self(void) {
    return reinterpret_cast<pthread_t>(Self());
}

int pthread_equal(pthread_t left, pthread_t right) {
    return left == right;
}

void pthread_exit(void*) {
    Libc::Fatal("pthread_exit unsupported; return from thread");
}

int pthread_setname_np(pthread_t, const char*) {
    return 0;
}

int pthread_getname_np(pthread_t, char* out_name, size_t size) {
    if (size == 0) {
        return ERANGE;
    }
    out_name[0] = '\0';
    return 0;
}

int pthread_setcancelstate(int, int* out_previous) {
    if (out_previous != nullptr) {
        *out_previous = PTHREAD_CANCEL_ENABLE;
    }
    return 0;
}

int pthread_setcanceltype(int, int* out_previous) {
    if (out_previous != nullptr) {
        *out_previous = PTHREAD_CANCEL_DEFERRED;
    }
    return 0;
}

void pthread_testcancel(void) {
}

int sched_yield(void) {
    ModLoader_Host_Thread_Sleep(0);
    return 0;
}

int __cxa_thread_atexit_impl(void (*function)(void*), void* object, void*) {
    Thread_Exit_Handler* handler = static_cast<Thread_Exit_Handler*>(malloc(sizeof(Thread_Exit_Handler)));

    if (handler == nullptr) {
        return -1;
    }
    handler->function = function;
    handler->object = object;
    handler->next = sThreadExitHandlers;
    sThreadExitHandlers = handler;
    return 0;
}

// For modules without the C++ library's own (which then takes precedence)
__attribute__((weak)) int __cxa_thread_atexit(void (*function)(void*), void* object, void* dso_handle) {
    return __cxa_thread_atexit_impl(function, object, dso_handle);
}

void __modloader_run_thread_atexit(void) {
    Run_Thread_Exit_Handlers();
    Run_Key_Destructors();
    Reap_Detached();
}

// attributes

int pthread_attr_init(pthread_attr_t* attributes) {
    attributes->detachState = PTHREAD_CREATE_JOINABLE;
    attributes->stackSize = 0;
    attributes->guardSize = 0;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t*) {
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t* attributes, int state) {
    if (state != PTHREAD_CREATE_JOINABLE && state != PTHREAD_CREATE_DETACHED) {
        return EINVAL;
    }
    attributes->detachState = state;
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t* attributes, int* out_state) {
    *out_state = attributes->detachState;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t* attributes, size_t size) {
    if (size < PTHREAD_STACK_MIN) {
        return EINVAL;
    }
    attributes->stackSize = size;
    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t* __restrict attributes, size_t* __restrict out_size) {
    *out_size = attributes->stackSize;
    return 0;
}

int pthread_attr_setguardsize(pthread_attr_t* attributes, size_t size) {
    attributes->guardSize = size;
    return 0;
}

int pthread_attr_getguardsize(const pthread_attr_t* __restrict attributes, size_t* __restrict out_size) {
    *out_size = attributes->guardSize;
    return 0;
}

// once and keys

int pthread_once(pthread_once_t* once, void (*routine)(void)) {
    // 0: not run, 1: running, 2: done
    uint32_t* state = reinterpret_cast<uint32_t*>(once);
    uint32_t expected = 0;

    if (__atomic_compare_exchange_n(state, &expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
        routine();
        __atomic_store_n(state, 2, __ATOMIC_RELEASE);
        Wake(state, gWakeAll);
        return 0;
    }
    while (expected != 2) {
        Wait(state, 1, gWaitForever);
        expected = __atomic_load_n(state, __ATOMIC_ACQUIRE);
    }
    return 0;
}

int pthread_key_create(pthread_key_t* out_key, void (*destructor)(void*)) {
    Libc::Scoped lock(sKeyLock);

    for (uint32_t index = 0; index < PTHREAD_KEYS_MAX; index++) {
        if (!sKeys[index].used) {
            sKeys[index].used = true;
            sKeys[index].generation++;
            sKeys[index].destructor = destructor;
            *out_key = index | (sKeys[index].generation << gKeyIndexBits);
            return 0;
        }
    }
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key) {
    uint32_t index = key & gKeyIndexMask;
    Libc::Scoped lock(sKeyLock);

    if (index >= PTHREAD_KEYS_MAX || !sKeys[index].used || sKeys[index].generation != key >> gKeyIndexBits) {
        return EINVAL;
    }
    sKeys[index].used = false;
    sKeys[index].destructor = nullptr;
    return 0;
}

void* pthread_getspecific(pthread_key_t key) {
    uint32_t index = key & gKeyIndexMask;

    if (index >= PTHREAD_KEYS_MAX || sKeyValues[index].generation != key >> gKeyIndexBits) {
        return nullptr;
    }
    return sKeyValues[index].value;
}

int pthread_setspecific(pthread_key_t key, const void* value) {
    uint32_t index = key & gKeyIndexMask;

    if (index >= PTHREAD_KEYS_MAX) {
        return EINVAL;
    }
    sKeyValues[index].generation = key >> gKeyIndexBits;
    sKeyValues[index].value = const_cast<void*>(value);
    return 0;
}

// mutexes

int pthread_mutexattr_init(pthread_mutexattr_t* attributes) {
    attributes->type = PTHREAD_MUTEX_DEFAULT;
    attributes->shared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t*) {
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t* attributes, int type) {
    if (type != PTHREAD_MUTEX_NORMAL && type != PTHREAD_MUTEX_RECURSIVE && type != PTHREAD_MUTEX_ERRORCHECK) {
        return EINVAL;
    }
    attributes->type = type;
    return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t* __restrict attributes, int* __restrict out_type) {
    *out_type = attributes->type;
    return 0;
}

int pthread_mutexattr_setpshared(pthread_mutexattr_t* attributes, int shared) {
    attributes->shared = shared;
    return 0;
}

int pthread_mutexattr_getpshared(const pthread_mutexattr_t* __restrict attributes, int* __restrict out_shared) {
    *out_shared = attributes->shared;
    return 0;
}

int pthread_mutex_init(pthread_mutex_t* __restrict mutex, const pthread_mutexattr_t* __restrict attributes) {
    mutex->state = Libc::Mutex_State::Unlocked;
    mutex->type = attributes != nullptr ? attributes->type : PTHREAD_MUTEX_DEFAULT;
    mutex->owner = 0;
    mutex->count = 0;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* mutex) {
    return mutex->state != Libc::Mutex_State::Unlocked ? EBUSY : 0;
}

int pthread_mutex_lock(pthread_mutex_t* mutex) {
    return Mutex_Lock(mutex, nullptr);
}

int pthread_mutex_timedlock(pthread_mutex_t* __restrict mutex, const struct timespec* __restrict deadline) {
    return Mutex_Lock(mutex, deadline);
}

int pthread_mutex_trylock(pthread_mutex_t* mutex) {
    unsigned long self = reinterpret_cast<unsigned long>(Self());
    uint32_t expected = Libc::Mutex_State::Unlocked;

    if (mutex->type == PTHREAD_MUTEX_RECURSIVE && __atomic_load_n(&mutex->owner, __ATOMIC_RELAXED) == self) {
        mutex->count++;
        return 0;
    }
    if (!__atomic_compare_exchange_n(&mutex->state, &expected, Libc::Mutex_State::Locked, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return EBUSY;
    }
    __atomic_store_n(&mutex->owner, self, __ATOMIC_RELAXED);
    mutex->count = 1;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t* mutex) {
    if (mutex->type != PTHREAD_MUTEX_NORMAL) {
        if (__atomic_load_n(&mutex->owner, __ATOMIC_RELAXED) != reinterpret_cast<unsigned long>(Self())) {
            return EPERM;
        }
        if (--mutex->count != 0) {
            return 0;
        }
    }
    __atomic_store_n(&mutex->owner, 0, __ATOMIC_RELAXED);
    mutex->count = 0;
    if (__atomic_exchange_n(&mutex->state, Libc::Mutex_State::Unlocked, __ATOMIC_RELEASE) == Libc::Mutex_State::Contended) {
        Wake(&mutex->state, 1);
    }
    return 0;
}

// conditions

int pthread_condattr_init(pthread_condattr_t* attributes) {
    attributes->clock = CLOCK_REALTIME;
    attributes->shared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_condattr_destroy(pthread_condattr_t*) {
    return 0;
}

int pthread_condattr_setclock(pthread_condattr_t* attributes, clockid_t clock) {
    if (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC) {
        return EINVAL;
    }
    attributes->clock = clock;
    return 0;
}

int pthread_condattr_getclock(const pthread_condattr_t* __restrict attributes, clockid_t* __restrict out_clock) {
    *out_clock = attributes->clock;
    return 0;
}

int pthread_condattr_setpshared(pthread_condattr_t* attributes, int shared) {
    attributes->shared = shared;
    return 0;
}

int pthread_condattr_getpshared(const pthread_condattr_t* __restrict attributes, int* __restrict out_shared) {
    *out_shared = attributes->shared;
    return 0;
}

int pthread_cond_init(pthread_cond_t* __restrict condition, const pthread_condattr_t* __restrict attributes) {
    condition->sequence = 0;
    condition->clock = attributes != nullptr ? attributes->clock : CLOCK_REALTIME;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t*) {
    return 0;
}

int pthread_cond_wait(pthread_cond_t* __restrict condition, pthread_mutex_t* __restrict mutex) {
    return Condition_Wait(condition, mutex, nullptr);
}

int pthread_cond_timedwait(pthread_cond_t* __restrict condition, pthread_mutex_t* __restrict mutex, const struct timespec* __restrict deadline) {
    return Condition_Wait(condition, mutex, deadline);
}

int pthread_cond_signal(pthread_cond_t* condition) {
    __atomic_fetch_add(&condition->sequence, 1, __ATOMIC_RELEASE);
    Wake(&condition->sequence, 1);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t* condition) {
    __atomic_fetch_add(&condition->sequence, 1, __ATOMIC_RELEASE);
    Wake(&condition->sequence, gWakeAll);
    return 0;
}

// read-write locks

int pthread_rwlockattr_init(pthread_rwlockattr_t* attributes) {
    attributes->shared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_rwlockattr_destroy(pthread_rwlockattr_t*) {
    return 0;
}

int pthread_rwlock_init(pthread_rwlock_t* __restrict lock, const pthread_rwlockattr_t* __restrict) {
    pthread_mutex_init(&lock->lock, nullptr);
    pthread_cond_init(&lock->changed, nullptr);
    lock->readers = 0;
    lock->writer = 0;
    return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t* lock) {
    return lock->readers != 0 || lock->writer != 0 ? EBUSY : 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t* lock) {
    return Read_Lock(lock, nullptr);
}

int pthread_rwlock_timedrdlock(pthread_rwlock_t* __restrict lock, const struct timespec* __restrict deadline) {
    return Read_Lock(lock, deadline);
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t* lock) {
    int result = EBUSY;

    Mutex_Lock(&lock->lock, nullptr);
    if (lock->writer == 0) {
        lock->readers++;
        result = 0;
    }
    pthread_mutex_unlock(&lock->lock);
    return result;
}

int pthread_rwlock_wrlock(pthread_rwlock_t* lock) {
    return Write_Lock(lock, nullptr);
}

int pthread_rwlock_timedwrlock(pthread_rwlock_t* __restrict lock, const struct timespec* __restrict deadline) {
    return Write_Lock(lock, deadline);
}

int pthread_rwlock_trywrlock(pthread_rwlock_t* lock) {
    int result = EBUSY;

    Mutex_Lock(&lock->lock, nullptr);
    if (lock->writer == 0 && lock->readers == 0) {
        lock->writer = 1;
        result = 0;
    }
    pthread_mutex_unlock(&lock->lock);
    return result;
}

int pthread_rwlock_unlock(pthread_rwlock_t* lock) {
    Mutex_Lock(&lock->lock, nullptr);
    if (lock->writer != 0) {
        lock->writer = 0;
    }
    else if (lock->readers != 0) {
        lock->readers--;
    }
    pthread_mutex_unlock(&lock->lock);
    pthread_cond_broadcast(&lock->changed);
    return 0;
}

// barriers and spins

int pthread_barrierattr_init(pthread_barrierattr_t* attributes) {
    attributes->shared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_barrierattr_destroy(pthread_barrierattr_t*) {
    return 0;
}

int pthread_barrier_init(pthread_barrier_t* __restrict barrier, const pthread_barrierattr_t* __restrict, unsigned int count) {
    if (count == 0) {
        return EINVAL;
    }
    pthread_mutex_init(&barrier->lock, nullptr);
    pthread_cond_init(&barrier->changed, nullptr);
    barrier->count = count;
    barrier->arrived = 0;
    barrier->generation = 0;
    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t*) {
    return 0;
}

int pthread_barrier_wait(pthread_barrier_t* barrier) {
    uint32_t generation;

    Mutex_Lock(&barrier->lock, nullptr);
    generation = barrier->generation;
    if (++barrier->arrived == barrier->count) {
        barrier->arrived = 0;
        barrier->generation++;
        pthread_mutex_unlock(&barrier->lock);
        pthread_cond_broadcast(&barrier->changed);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }
    while (barrier->generation == generation) {
        Condition_Wait(&barrier->changed, &barrier->lock, nullptr);
    }
    pthread_mutex_unlock(&barrier->lock);
    return 0;
}

int pthread_spin_init(pthread_spinlock_t* lock, int) {
    *lock = 0;
    return 0;
}

int pthread_spin_destroy(pthread_spinlock_t*) {
    return 0;
}

int pthread_spin_lock(pthread_spinlock_t* lock) {
    while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE) != 0) {
        while (__atomic_load_n(lock, __ATOMIC_RELAXED) != 0) {
        }
    }
    return 0;
}

int pthread_spin_trylock(pthread_spinlock_t* lock) {
    return __atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE) == 0 ? 0 : EBUSY;
}

int pthread_spin_unlock(pthread_spinlock_t* lock) {
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
    return 0;
}

} // extern "C"
