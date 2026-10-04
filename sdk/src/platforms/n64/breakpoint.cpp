#include <modloader/platforms/n64/breakpoint.h>
#include <modloader_n64_adapter.h>

using namespace ModLoader;
using namespace ModLoader::N64;

Guest::Breakpoint::Handler Breakpoint::Adapt(Handler handler) {
    if (!handler) {
        return {};
    }

    return [handler = std::move(handler)](const Hit& hit, std::span<u8> state) {
        return state.size() == sizeof(Cpu_State) &&
            handler(hit, *reinterpret_cast<Cpu_State*>(state.data()));
    };
}

bool Breakpoint::Create(Guest::uptr address, Guest::usize size, u32 access, Handler handler) {
    return Guest::Breakpoint::Create(MODLOADER_N64_PROCESSOR_VR4300, address, size, access, Adapt(std::move(handler)));
}

Breakpoint::Handle Breakpoint::Add(Guest::uptr address, Guest::usize size, u32 access, Handler handler) {
    return Guest::Breakpoint::Add(MODLOADER_N64_PROCESSOR_VR4300, address, size, access, Adapt(std::move(handler)));
}

bool Breakpoint::Step(Handler handler) {
    return Guest::Breakpoint::Step(MODLOADER_N64_PROCESSOR_VR4300, Adapt(std::move(handler)));
}

std::optional<Cpu_State> Breakpoint::Get_Cpu() {
    Cpu_State state{};
    if (!Guest::Breakpoint::Get_Cpu(MODLOADER_N64_PROCESSOR_VR4300,
        {reinterpret_cast<u8*>(&state), sizeof(state)})) {
        return std::nullopt;
    }
    return state;
}

bool Breakpoint::Set_Cpu(const Cpu_State& state) {
    return Guest::Breakpoint::Set_Cpu(MODLOADER_N64_PROCESSOR_VR4300, {reinterpret_cast<const u8*>(&state), sizeof(state)});
}
