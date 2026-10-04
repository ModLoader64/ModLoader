#include "../internal.h"

#include <modloader/async/thread.h>

#include <pthread.h>
#include <exception>
#include <memory>
#include <new>
#include <utility>


namespace {

constexpr s64 gWaitForever = -1;

s32 Wait(u32* address, u32 expected, s64 timeout_nanoseconds) {
    return __builtin_wasm_memory_atomic_wait32(reinterpret_cast<int*>(address), static_cast<int>(expected), timeout_nanoseconds);
}

void Notify(u32* address, u32 count) {
    __builtin_wasm_memory_atomic_notify(reinterpret_cast<int*>(address), count);
}

void* Thread_Main(void* argument) {
    std::unique_ptr<ModLoader::Function<void()>> entry(static_cast<ModLoader::Function<void()>*>(argument));
    (*entry)();
    return nullptr;
}

} // namespace

ModLoader::Thread::Handle ModLoader::Thread::Start(Function<void()> entry) {
    if (!entry) {
        return Handle();
    }

    std::unique_ptr<Function<void()>> callable(new (std::nothrow) Function<void()>(std::move(entry)));
    pthread_t thread;
    if (!callable || pthread_create(&thread, nullptr, Thread_Main, callable.get()) != 0) {
        return {};
    }

    callable.release();
    return Handle(static_cast<u64>(thread));
}

ModLoader::Thread::Handle::~Handle() {
    if (id != 0) {
        std::terminate();
    }
}

ModLoader::Thread::Handle::Handle(Handle&& other) noexcept : id(std::exchange(other.id, 0)) {
}

ModLoader::Thread::Handle& ModLoader::Thread::Handle::operator=(Handle&& other) noexcept {
    if (this != &other) {
        if (id != 0) {
            std::terminate();
        }
        id = std::exchange(other.id, 0);
    }

    return *this;
}

bool ModLoader::Thread::Handle::Join() {
    if (id == 0 || pthread_join(static_cast<pthread_t>(id), nullptr) != 0) {
        return false;
    }
    id = 0;
    return true;
}

bool ModLoader::Thread::Handle::Detach() {
    if (id == 0 || pthread_detach(static_cast<pthread_t>(id)) != 0) {
        return false;
    }
    id = 0;
    return true;
}

void ModLoader::Thread::Sleep(u32 milliseconds) {
    ModLoader_Host_Thread_Sleep(milliseconds);
}

bool ModLoader::Thread::Is_Emulation_Thread() {
    return ModLoader_Host_Thread_Is_Emulation() != 0;
}

u64 ModLoader::Thread::Milliseconds() {
    return ModLoader_Host_Time_Milliseconds();
}

void ModLoader::Sync::Mutex::Lock() {
    u32 expected = State::Unlocked;

    if (__atomic_compare_exchange_n(&state, &expected, State::Locked, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return;
    }

    if (expected != State::Contended) {
        expected = __atomic_exchange_n(&state, State::Contended, __ATOMIC_ACQUIRE);
    }
    
    while (expected != State::Unlocked) {
        Wait(&state, State::Contended, gWaitForever);
        expected = __atomic_exchange_n(&state, State::Contended, __ATOMIC_ACQUIRE);
    }
}

bool ModLoader::Sync::Mutex::Try_Lock() {
    u32 expected = State::Unlocked;

    return __atomic_compare_exchange_n(&state, &expected, State::Locked, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

void ModLoader::Sync::Mutex::Unlock() {
    if (__atomic_exchange_n(&state, State::Unlocked, __ATOMIC_RELEASE) == State::Contended) {
        Notify(&state, 1);
    }
}
