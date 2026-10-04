#include "../../internal.h"

#include <modloader/platforms/n64/cpu.h>
#include <modloader/platforms/n64/component.h>
#include <modloader/detail/component.h>
#include <modloader/guest/machine.h>
#include <modloader/logger.h>
#include <modloader/async/timer.h>
#include <modloader_n64_module.h>
#include <modloader_n64_adapter.h>

#include <string.h>

using namespace ModLoader;

namespace {

struct Platform_Events {
#define MODLOADER_LIFECYCLE(field, arguments, pre, normal, post, event) Runtime::Component_Callbacks<void arguments> field;
#include <modloader/platforms/n64/lifecycle.def>
#undef MODLOADER_LIFECYCLE
};

constinit Platform_Events sEvents;

constexpr Guest::uptr gKseg0 = 0x80000000u;
constexpr Guest::uptr gKseg1 = 0xA0000000u;
constexpr Guest::uptr gSegmentMask = 0xE0000000u;
constexpr Guest::uptr gPhysicalMask = 0x1FFFFFFFu;
constexpr Guest::uptr gKseg1RamEnd = 0x03F00000u; // RCP register boundary
constexpr Guest::uptr gRomCpu = 0xB0000000u;
constexpr Guest::uptr gRomCpuEnd = 0x0FC00000u; // PIF boundary
constexpr Guest::uptr gRomPi = 0x10000000u;

static_assert(sizeof(ModLoader::N64::Cpu_State) == sizeof(ModLoader_N64_Cpu_State), "sizeof(ModLoader::N64::Cpu_State) == sizeof(ModLoader_N64_Cpu_State)");

ModLoader::Platform sPlatform;

bool Ram_Contains(Guest::uptr address, Guest::usize limit) {
    Guest::uptr segment = address & gSegmentMask;
    Guest::uptr physical = address & gPhysicalMask;

    return physical < limit && (segment == gKseg0 || (segment == gKseg1 && physical < gKseg1RamEnd));
}

void Set_Rom_Size(u64 size) {
    sPlatform.romEnd = gRomCpu + static_cast<Guest::uptr>(size < gRomCpuEnd ? size : gRomCpuEnd);
    sPlatform.rom.size = size;
}

bool Find_Space(const ModLoader_Platform_Description* description, const char* name, ModLoader_Module_Space* space) {
    for (u32 index = 0; index < description->spaceCount; index++) {
        if (Machine::Space_Describe(index, space) && strcmp(space->name, name) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

const ModLoader::Platform& ModLoader::gPlatform = sPlatform;

bool ModLoader::Runtime::Platform_Initialize(const ModLoader_Platform_Description* description) {
    ModLoader_Module_Space rdram;
    ModLoader_Module_Space rom;
    ModLoader_N64_Image image;

    if ((description->session & MODLOADER_SESSION_GAME) == 0) {
        return true; // dedicated server
    }

    if (strcmp(description->platform, "n64") != 0 ||
        !Find_Space(description, "rdram", &rdram) || !Find_Space(description, "rom", &rom) ||
        Machine::Query(MODLOADER_N64_QUERY_IMAGE, &image, sizeof(image)) != sizeof(image)) {
        Logger::Error("N64 initialization failed");
        return false;
    }
    
    sPlatform.ramStart = gKseg0;
    sPlatform.ramEnd = gKseg0 + static_cast<Guest::uptr>(rdram.reportedSize);
    sPlatform.extendedRamEnd = gKseg0 + static_cast<Guest::uptr>(rdram.usableSize);
    sPlatform.romStart = gRomCpu;
    Set_Rom_Size(rom.usableSize);
    memcpy(sPlatform.rom.sha1, image.sha1, sizeof(sPlatform.rom.sha1));
    memcpy(sPlatform.rom.title, image.title, sizeof(sPlatform.rom.title));
    memcpy(sPlatform.rom.gameCode, image.gameCode, sizeof(sPlatform.rom.gameCode));
    sPlatform.rom.sha1[sizeof(sPlatform.rom.sha1) - 1] = 0;
    sPlatform.rom.title[sizeof(sPlatform.rom.title) - 1] = 0;
    sPlatform.rom.gameCode[sizeof(sPlatform.rom.gameCode) - 1] = 0;
    sPlatform.rom.version = image.version;
    return true;
}

void ModLoader::Runtime::Platform_Start() {
#define MODLOADER_LIFECYCLE(field, arguments, pre, normal, post, event) \
    sEvents.field.Prepare([](const Component* component) { \
        const auto* platform = static_cast<const N64_Component*>(component->platform); \
        return platform != nullptr ? &platform->field : nullptr; \
    }); \
    if (sEvents.field.Any()) { \
        Follow_Event(event, true); \
    }
#include <modloader/platforms/n64/lifecycle.def>
#undef MODLOADER_LIFECYCLE
    Follow_Event(MODLOADER_EVENT_IMAGE_RESIZED, true);
}

bool ModLoader::Runtime::Platform_Clock_Event(u32 clock, u32* event) {
    switch (static_cast<Clock>(clock)) {
    case N64::Clocks::VI:
        *event = MODLOADER_EVENT_REFRESH;
        return true;
    case N64::Clocks::Frame:
        *event = MODLOADER_EVENT_FRAME;
        return true;
    case N64::Clocks::Task:
        *event = MODLOADER_EVENT_N64_RSP_TASK;
        return true;
    default:
        return false;
    }
}

void ModLoader::Runtime::Platform_Event(ModLoader_Event_Phase phase, u32 event, const void* record, u64 size) {
    if (event == MODLOADER_EVENT_REFRESH && size >= sizeof(ModLoader_N64_Vi_Record)) {
        ModLoader_N64_Vi_Record vi_record;
        N64::Vertical_Interrupt vi;

        memcpy(&vi_record, record, sizeof(vi_record));
        vi = { vi_record.viCount, vi_record.viTime, vi_record.status, vi_record.origin, vi_record.width };
        sEvents.vi.Call(phase, &vi);
        if (phase == MODLOADER_EVENT_PHASE_NORMAL) {
            Advance_Clock(static_cast<u32>(N64::Clocks::VI), vi.viCount);
        }
    }
    else if (event == MODLOADER_EVENT_IMAGE_RESIZED && size >= sizeof(ModLoader_Image_Resized_Record)) {
        ModLoader_Image_Resized_Record resized;

        memcpy(&resized, record, sizeof(resized));
        if (phase == MODLOADER_EVENT_PHASE_NORMAL) {
            Set_Rom_Size(resized.size);
        }
    }
    else if (event == MODLOADER_EVENT_FRAME && size >= sizeof(ModLoader_N64_Frame_Record)) {
        ModLoader_N64_Frame_Record frame_record;
        Frame frame;

        memcpy(&frame_record, record, sizeof(frame_record));
        frame = { frame_record.frameCount, frame_record.frameTime, frame_record.origin, frame_record.width };
        sEvents.frame.Call(phase, &frame);
        if (phase == MODLOADER_EVENT_PHASE_NORMAL) {
            Advance_Clock(static_cast<u32>(N64::Clocks::Frame), frame.frameCount);
        }
    }
    else if (event == MODLOADER_EVENT_N64_RSP_TASK && size >= sizeof(ModLoader_N64_Task_Record)) {
        ModLoader_N64_Task_Record task_record;
        N64::Task task;

        memcpy(&task_record, record, sizeof(task_record));
        task = { task_record.taskCount, task_record.taskTime };
        sEvents.task.Call(phase, &task);
        if (phase == MODLOADER_EVENT_PHASE_NORMAL) {
            Advance_Clock(static_cast<u32>(N64::Clocks::Task), task.taskCount);
        }
    }
}

Guest::uptr ModLoader::Platform::Ram_Address(this const Platform& self, Guest::uptr address) {
    (void)self;
    return gKseg0 | (address & gPhysicalMask);
}

bool ModLoader::Platform::Is_Ram(this const Platform& self, Guest::uptr address) {
    return Ram_Contains(address, self.ramEnd - self.ramStart);
}

bool ModLoader::Platform::Is_Extended_Ram(this const Platform& self, Guest::uptr address) {
    return Ram_Contains(address, self.extendedRamEnd - self.ramStart);
}

bool ModLoader::Platform::Resize_Rom(this const Platform& self, Guest::usize bytes) {
    (void)self;
    if (ModLoader_Host_Image_Resize(bytes) != 0) {
        return false;
    }
    Set_Rom_Size(bytes);
    return true;
}

bool ModLoader::Platform::Is_Rom(this const Platform& self, Guest::uptr address) {
    Guest::usize size = self.romEnd - self.romStart;

    return address - gRomCpu < size || address - gRomPi < size;
}

Guest::uptr ModLoader::Platform::Translate(this const Platform& self, Guest::uptr address) {
    (void)self;
    return static_cast<Guest::uptr>(ModLoader_Host_Translate_Address(MODLOADER_N64_PROCESSOR_VR4300, address));
}



