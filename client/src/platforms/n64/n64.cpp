#include "n64.h"

#include <string.h>

namespace {

constexpr u32 gSegmentMask = 0xE0000000u;
constexpr u32 gPhysicalMask = 0x1FFFFFFFu;
constexpr u32 gPhysicalRamLimit = 0x20000000u;

// Buttons follow PIF bit order; stick directions follow the buttons.
constexpr const char* gControls[] = {
    "dpad_right", "dpad_left", "dpad_down", "dpad_up", "start", "z", "b", "a", "c_right",
    "c_left", "c_down", "c_up", "r", "l", "stick_right", "stick_left", "stick_up", "stick_down"
};
constexpr u32 gButtonCount = 14;
constexpr u32 gStickRight = 14;
constexpr u32 gStickLeft = 15;
constexpr u32 gStickUp = 16;
constexpr u32 gStickDown = 17;
constexpr f32 gStickRange = 80.0f;

// VR4300 registers sign-extend 32-bit addresses
std::optional<u32> Cpu_Address(u64 address) {
    return address <= 0xFFFFFFFFull || (address >> 32) == 0xFFFFFFFFull ? std::optional(static_cast<u32>(address)) : std::nullopt;
}

u32 Kseg0_Of(u32 address) {
    return address != 0 && address < gPhysicalRamLimit ? gKseg0 | address : address;
}

}

f32 Event_Clock::Tick(u64 time) {
    f32 seconds = lastSeconds;

    if (timeKnown && time >= lastTime) {
        seconds = static_cast<f32>(static_cast<f64>(time - lastTime) / 1e9);
    }
    else if (!timeKnown && count == 0) {
        seconds = static_cast<f32>(static_cast<f64>(time) / 1e9); // first event: since power-on
    }

    count++;
    lastTime = time;
    lastSeconds = seconds;
    timeKnown = true;
    return seconds;
}

N64_Platform::N64_Platform() {
    identifier = "n64";
    ramSpace = "rdram";
    imageSpace = "rom";
    bigEndian = true;
    savestates = true;
    lastHypercall = 0xFFFFF;
    clockCount = MODLOADER_N64_CLOCK_FRAME;
    controls = gControls;
}

namespace {
const bool sRegistered = [] {
    Platform_Register("n64", []() -> std::unique_ptr<Platform> {
        return std::make_unique<N64_Platform>();
    });
    return true;
}();
} // namespace

bool N64_Platform::Start(Runtime& owner) {
    runtime = &owner;
    if (owner.config.image != nullptr) {
        statePath = Path_Join(Path_Join(owner.config.dataDirectory, "states"), owner.config.image->sha1 + ".state");
    }
    romSpace = static_cast<u32>(owner.config.spaces.size());
    for (u32 index = 0; index < owner.config.spaces.size(); index++) {
        if (strcmp(owner.config.spaces[index].name, "rom") == 0) {
            romSpace = index;
        }
    }
    return true;
}

bool N64_Platform::Map_Address(u32 processor, u64 address, u32& out_space, u64& out_offset) const {
    std::optional<u32> cpu_address = Cpu_Address(address);
    const ModLoader_Space_Descriptor& ram = runtime->Ram();
    const ModLoader_Space_Descriptor* rom = Rom();
    u32 segment;
    u32 physical;

    if (processor != MODLOADER_N64_PROCESSOR_VR4300 || !cpu_address) {
        return false;
    }

    segment = *cpu_address & gSegmentMask;
    physical = *cpu_address & gPhysicalMask;
    if ((segment == gKseg0 || (segment == gKseg1 && physical < gKseg1RamEnd)) && physical < ram.usableSize) {
        out_space = runtime->ramSpace;
        out_offset = physical;
        return true;
    }

    if (rom != nullptr && *cpu_address - gRomCpu < rom->usableSize && *cpu_address - gRomCpu < gRomCpuEnd) {
        out_space = romSpace;
        out_offset = *cpu_address - gRomCpu;
        return true;
    }

    // PI
    if (rom != nullptr && *cpu_address - gRomPi < rom->usableSize) {
        out_space = romSpace;
        out_offset = *cpu_address - gRomPi;
        return true;
    }

    return false;
}

u64 N64_Platform::Fixed_Address(u32 processor, u32 space, u64 offset) const {
    if (processor != MODLOADER_N64_PROCESSOR_VR4300) {
        return 0;
    }

    if (space == runtime->ramSpace && offset < gPhysicalRamLimit) {
        return gKseg0 | offset;
    }

    if (space == romSpace && offset < 0x10000000ull) {
        return offset < gRomCpuEnd ? gRomCpu + offset : gRomPi + offset;
    }

    return 0;
}

void N64_Platform::Event_Record(u32 event, u64 time, const void* data, u64 size, std::vector<u8>& out_record) {
    ModLoader_N64_Vi screen = {};
    bool has_vi = data != nullptr && size >= sizeof(ModLoader_N64_Vi);

    if (has_vi) {
        memcpy(&screen, data, sizeof(screen));
    }

    if (event == MODLOADER_EVENT_REFRESH) {
        ModLoader_N64_Vi_Record record = {};

        record.viTime = vi.Tick(time);
        record.viCount = vi.count;
        if (has_vi) {
            record.status = screen.status;
            record.origin = Kseg0_Of(screen.origin & 0x07FFFFFFu);
            record.width = screen.width;
        }
        out_record.resize(sizeof(record));
        memcpy(out_record.data(), &record, sizeof(record));
        return;
    }

    if (event == MODLOADER_EVENT_N64_RSP_TASK) {
        ModLoader_N64_Task_Record record = {};

        record.taskTime = tasks.Tick(time);
        record.taskCount = tasks.count;
        out_record.resize(sizeof(record));
        memcpy(out_record.data(), &record, sizeof(record));
        return;
    }

    if (event == MODLOADER_EVENT_FRAME) {
        ModLoader_N64_Frame_Record record = {};

        record.frameTime = frames.Tick(time);
        record.frameCount = frames.count;
        if (has_vi) {
            record.origin = Kseg0_Of(screen.origin & 0x07FFFFFFu);
            record.width = screen.width;
        }
        out_record.resize(sizeof(record));
        memcpy(out_record.data(), &record, sizeof(record));
        return;
    }
    Platform::Event_Record(event, time, data, size, out_record);
}

u64 N64_Platform::Clock_Now(u32 clock) const {
    switch (clock) {
    case MODLOADER_N64_CLOCK_VI:
        return vi.count;
    case MODLOADER_N64_CLOCK_TASK:
        return tasks.count;
    case MODLOADER_N64_CLOCK_FRAME:
        return frames.count;
    default:
        return 0;
    }
}

void N64_Platform::Reset() {
    vi = {};
    tasks = {};
    frames = {};
}

void N64_Platform::State_Loaded() {
    Restore_Image();
    vi.timeKnown = false;
    tasks.timeKnown = false;
    frames.timeKnown = false;
}

void N64_Platform::Pack_Controls(std::span<const f32> values, std::vector<u8>& out_state) const {
    u32 state = 0;
    s32 stick_x = static_cast<s32>((values[gStickRight] - values[gStickLeft]) * gStickRange);
    s32 stick_y = static_cast<s32>((values[gStickUp] - values[gStickDown]) * gStickRange);

    for (u32 button = 0; button < gButtonCount; button++) {
        state |= values[button] >= 0.5f ? 1u << button : 0;
    }

    state |= static_cast<u32>(static_cast<u8>(static_cast<s8>(stick_x))) << MODLOADER_N64_STICK_X_SHIFT | static_cast<u32>(static_cast<u8>(static_cast<s8>(stick_y))) << MODLOADER_N64_STICK_Y_SHIFT;
    out_state.resize(sizeof(state));
    memcpy(out_state.data(), &state, sizeof(state));
}

s64 N64_Platform::Query(Runtime& owner, u32 query, void* output, u64 capacity) {
    if (query != MODLOADER_N64_QUERY_IMAGE) {
        return Platform::Query(owner, query, output, capacity);
    }

    if (capacity >= sizeof(image) && output != nullptr) {
        memcpy(output, &image, sizeof(image));
    }

    return sizeof(image);
}

