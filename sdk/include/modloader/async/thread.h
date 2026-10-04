#pragma once

#include <modloader/function.h>
#include <modloader/types.h>

namespace ModLoader::Thread {

// Owns a joinable thread
// Join or detach before destruction or move-assignment; otherwise std::terminate is called
class Handle {
public:
    Handle() = default;
    explicit Handle(u64 id)
        : id(id) {
    }

    ~Handle();

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept;
    Handle& operator=(Handle&& other) noexcept;

    bool Join();   // Wait for completion and release the handle, returns false on failure
    bool Detach(); // Release the handle without waiting

    bool Is_Valid() const {
        return id != 0;
    }

private:
    u64 id = 0; // the thread's pthread_t
};

// Copies/owns the callable; returns an invalid handle on failure
Handle Start(Function<void()> entry);

void Sleep(u32 milliseconds); // Blocks the calling thread
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
