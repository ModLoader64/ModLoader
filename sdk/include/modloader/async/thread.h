#pragma once

#include <modloader/types.h>

namespace ModLoader::Thread {

bool Is_Emulation_Thread();

u64 Milliseconds(); // monotonic clock

} // namespace ModLoader::Thread

namespace ModLoader::Sync {

// Non-recursive lock shared by this module's threads. Do not destroy it while locked or while threads are waiting.
class Mutex {
public:
    Mutex() = default;
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void Lock();
    bool Try_Lock();
    void Unlock();

private:
    enum State : u32 {
        Unlocked = 0,
        Locked = 1,
        Contended = 2,
    };

    u32 state = State::Unlocked;
};

// Locks on construction and unlocks on destruction
class Scoped_Lock {
public:
    explicit Scoped_Lock(Mutex& mutex) : mutex(mutex) {
        mutex.Lock();
    }

    ~Scoped_Lock() {
        mutex.Unlock();
    }

    Scoped_Lock(const Scoped_Lock&) = delete;
    Scoped_Lock& operator=(const Scoped_Lock&) = delete;

private:
    Mutex& mutex;
};

} // namespace ModLoader::Sync
