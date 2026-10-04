#pragma once

#include <modloader/function.h>
#include <modloader/types.h>
#include <modloader_debug.h>

#include <span>

namespace ModLoader::Guest {

// Owns a platform breakpoint; destruction removes it
// Platform classes supply typed addresses and CPU state, ex: N64::Breakpoint
class Breakpoint {
public:
    using Handle = u32;

    // Combine Read | Write to watch both accesses
    enum Access : u32 {
        Execute = MODLOADER_BREAK_EXECUTE,
        Read = MODLOADER_BREAK_READ,
        Write = MODLOADER_BREAK_WRITE,
    };

    using Hit = ModLoader_Break;

    constexpr Breakpoint() noexcept = default;
    virtual ~Breakpoint();
    Breakpoint(const Breakpoint&) = delete;
    Breakpoint& operator=(const Breakpoint&) = delete;
    Breakpoint(Breakpoint&& other) noexcept;
    Breakpoint& operator=(Breakpoint&& other) noexcept;

    // Creation starts disabled
    // Enable/Disable keep the stored callback and address range
    bool Enable();
    bool Disable();
    bool Destroy();
    bool Is_Created() const;
    bool Is_Enabled() const;
    Handle Id() const; // Host ID while enabled; 0 otherwise, may change after reenabling

    static void Remove(Handle id); // Removes an enabled breakpoint returned by Add or Id
    static void Resume(); // Resume execution and cancel a pending step
    static bool Paused();

protected:
    // Mutable CPU state bytes in the platform's format; return true to pause
    using Handler = Function<bool(const Hit&, std::span<u8>)>;

    // Processor ID comes from Machine::Processor_Describe
    bool Create(u32 processor, u64 address, u64 size, u32 access, Handler handler);
    static Handle Add(u32 processor, u64 address, u64 size, u32 access, Handler handler);
    static bool Pause(u32 processor, Handler handler); // Stop before the next instruction while running
    static bool Step(u32 processor, Handler handler);  // Execute one instruction while paused, then call handler
    // Only while paused
    static bool Get_Cpu(u32 processor, std::span<u8> state);
    static bool Set_Cpu(u32 processor, std::span<const u8> state);

private:
    u64 token = 0;
};

} // namespace ModLoader::Guest 
