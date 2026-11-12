#include "../internal.h"

#include <modloader/async/thread.h>

namespace {

constexpr s64 gWaitForever = -1;

s32 Wait(u32* address, u32 expected, s64 timeout_nanoseconds) {
    return __builtin_wasm_memory_atomic_wait32(reinterpret_cast<int*>(address), static_cast<int>(expected), timeout_nanoseconds);
}

void Notify(u32* address, u32 count) {
    __builtin_wasm_memory_atomic_notify(reinterpret_cast<int*>(address), count);
}

} // namespace

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
