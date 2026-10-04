#pragma once

#include <modloader/guest/breakpoint.h>
#include <modloader/guest/types.h>
#include <modloader/platforms/n64/cpu.h>

#include <optional>

namespace ModLoader::N64 {

// VR4300 breakpoints/watchpoints
// The object owns its callback and starts disabled
class Breakpoint : public Guest::Breakpoint {
public:
    // Runs before state.pc; true pauses the CPU
    // Changing pc skips the stopped instruction
    using Handler = Function<bool(const Hit& hit, Cpu_State& state)>;

    // Watches [address, address + size)
    bool Create(Guest::uptr address, Guest::usize size, u32 access, Handler handler);
    // Creates an enabled breakpoint without an owning object; pair its nonzero ID with Remove
    static Handle Add(Guest::uptr address, Guest::usize size, u32 access, Handler handler);

    // Only while paused
    static bool Step(Handler handler);
    static std::optional<Cpu_State> Get_Cpu();
    static bool Set_Cpu(const Cpu_State& state);

private:
    static Guest::Breakpoint::Handler Adapt(Handler handler);
};

} // namespace ModLoader::N64
