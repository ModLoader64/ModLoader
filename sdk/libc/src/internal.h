#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../../src/host_imports.h"
#define LIBC_EXPORT(name) __attribute__((export_name(name)))

namespace Libc {

constexpr uint32_t gLogError = 0;
constexpr uint32_t gLogInfo = 2;

// Clocks for ModLoader_Host_Time_Now (microseconds)
constexpr uint32_t gClockMonotonic = 0;
constexpr uint32_t gClockRealtime = 1;

// Return 0 to continue, -1 to stop formatting
using Format_Sink = int (*)(const char* text, size_t length, void* context);
int Format_To(Format_Sink sink, void* context, const char* format, __builtin_va_list arguments);

// Logs the message and stops the module
[[noreturn]] void Fatal(const char* message);

enum Mutex_State : uint32_t {
    Unlocked = 0,
    Locked = 1,
    Contended = 2,
};

// Sleeps on wasm atomics when contended
class Spin_Lock {
public:
    void Lock();
    void Unlock();

private:
    uint32_t state = Mutex_State::Unlocked;
};

class Scoped {
public:
    explicit Scoped(Spin_Lock& lock)
        : lock(lock) {
        lock.Lock();
    }

    ~Scoped() {
        lock.Unlock();
    }

    Scoped(const Scoped&) = delete;
    Scoped& operator=(const Scoped&) = delete;

private:
    Spin_Lock& lock;
};

// Return the byte count (1-4), or 0 for invalid/incomplete input
size_t Utf8_Decode(const unsigned char* text, size_t size, uint32_t* out_code_point);
size_t Utf8_Encode(uint32_t code_point, char* out_text);

} // namespace Libc
