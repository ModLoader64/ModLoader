#include "ui/imgui_bindings.h"
#include <modloader.h>
#include <modloader/detail/component.h>
#include <cstring>
#include "internal.h"

#include <modloader/async/timer.h>

#include <stdlib.h>

extern "C" {

void __wasm_call_ctors();

void ModLoader_Event_Init() asm("modloader_event_init");
void ModLoader_Event_Shutdown() asm("modloader_event_shutdown");
void ModLoader_Ui_Phase(u32 phase) asm("modloader_ui_phase");
u64 ModLoader_Ui_Event_Buffer(u64 capacity) asm("modloader_ui_event_buffer");
u32 ModLoader_Ui_Event(u64 listener, u64 size) asm("modloader_ui_event");
u64 ModLoader_Event_Buffer(u64 capacity) asm("modloader_event_buffer");
void ModLoader_Event_Phase_Handle(u32 phase, u32 event, u64 size) asm("modloader_event_phase");
u32 ModLoader_Event_Hypercall(u64 handler, u32 processor, u64 size) asm("modloader_event_hypercall");
u32 ModLoader_Event_Breakpoint(u64 handler, u32 processor, u64 size) asm("modloader_event_breakpoint");
void ModLoader_Dispatch() asm("modloader_dispatch");

} // extern "C"

u32 ModLoader::Runtime::gSession;
constinit ModLoader::Runtime::Component_Events ModLoader::Runtime::gComponentEvents;

bool ModLoader::Game_Running() {
    return (Runtime::gSession & MODLOADER_SESSION_GAME) != 0;
}

namespace {

using ModLoader::Runtime::gComponentEvents;
using ModLoader::Runtime::Component;

struct Buffer {
    u8* data;
    u64 capacity;
    Buffer* next;
};

struct Event_Subscription {
    Event_Subscription* next;
    u32 event;
    u32 followers;
};

Buffer sEventBuffer;
Buffer sUiEventBuffer;
u32 sEventDepth;
u32 sUiDepth;
Event_Subscription* sSubscriptions;

Buffer* At_Depth(Buffer* buffer, u32 depth) {
    for (u32 index = 0; index < depth; index++) {
        if (buffer->next == nullptr) {
            buffer->next = static_cast<Buffer*>(calloc(1, sizeof(Buffer)));
            if (buffer->next == nullptr) {
                return nullptr;
            }
        }
        buffer = buffer->next;
    }
    return buffer;
}

u64 Reserve(Buffer* buffer, u32 depth, u64 capacity) {
    buffer = At_Depth(buffer, depth);
    if (buffer == nullptr) {
        return 0;
    }

    if (capacity > buffer->capacity) {
        u8* data = static_cast<u8*>(realloc(buffer->data, capacity));
        if (data == nullptr) {
            return 0;
        }
        buffer->data = data;
        buffer->capacity = capacity;
    }
    return reinterpret_cast<u64>(buffer->data);
}

void Finish_Event() {
    ModLoader::Runtime::Dispatch();
    if (sEventDepth <= 1) {
        ModLoader::Runtime::Scratch_Reset();
    }
}

} // namespace

bool ModLoader::Runtime::Follow_Event(u32 event, bool follow) {
    Event_Subscription** link = &sSubscriptions;
    while (*link != nullptr && (*link)->event != event) {
        link = &(*link)->next;
    }

    Event_Subscription* subscription = *link;
    if (!follow) {
        if (subscription != nullptr && --subscription->followers == 0) {
            *link = subscription->next;
            free(subscription);
            ModLoader_Host_Event_Subscribe(event, 0);
        }
        return true;
    }

    if (subscription != nullptr) {
        if (subscription->followers == UINT32_MAX) {
            return false;
        }
        subscription->followers++;
        return true;
    }

    subscription = static_cast<Event_Subscription*>(malloc(sizeof(Event_Subscription)));
    if (subscription == nullptr) {
        return false;
    }
    *subscription = { nullptr, event, 1 };
    *link = subscription;
    ModLoader_Host_Event_Subscribe(event, 1);
    return true;
}

extern "C" {

MODLOADER_EXPORT("modloader_event_init") void ModLoader_Event_Init() {
    ModLoader_Platform_Description description = {};

    ModLoader_Host_Platform_Describe(&description, sizeof(description));
    if (description.abiVersion != MODLOADER_MODULE_ABI_VERSION) {
        ModLoader::Logger::Error("module ABI %u; client ABI %u", MODLOADER_MODULE_ABI_VERSION, description.abiVersion);
        abort();
    }

    ModLoader::Runtime::gSession = description.session;
    if (!ModLoader::Runtime::Platform_Initialize(&description)) {
        abort();
    }

    sEventDepth++;
    ModLoader::Runtime::Prepare_Components();

    if (ModLoader_Host_Bind_Symbols() == 0) {
        sEventDepth--;
        return;
    }

    __wasm_call_ctors();
    ModLoader::Runtime::Platform_Start();
#define MODLOADER_LIFECYCLE(field, arguments, pre, normal, post, event) \
    if (event != 0 && gComponentEvents.field.Any()) { \
        ModLoader::Runtime::Follow_Event(event, true); \
    }
#include <modloader/detail/lifecycle.def>
#undef MODLOADER_LIFECYCLE
    gComponentEvents.init.Call_All();
    Finish_Event();
    sEventDepth--;
}

MODLOADER_EXPORT("modloader_event_shutdown") void ModLoader_Event_Shutdown() {
    sEventDepth++;
    ModLoader::Runtime::Stop_Tasks();
    Finish_Event();
    ModLoader::Runtime::Promises_Shutdown();
    gComponentEvents.shutdown.Call_All(true);
    ModLoader::Runtime::Net_Shutdown();
    ModLoader::Runtime::Timers_Shutdown();
    ModLoader::Runtime::Listeners_Shutdown();
    ModLoader::Runtime::Breakpoints_Shutdown();
    ModLoader::Runtime::Textures_Shutdown();
    ModLoader::Runtime::Scratch_Reset();
    __modloader_run_thread_atexit();
    __modloader_run_atexit();
    if (ModLoader::Runtime::Platform_Shutdown != nullptr) {
        ModLoader::Runtime::Platform_Shutdown();
    }
    sEventDepth--;
}

MODLOADER_EXPORT("modloader_ui_phase") void ModLoader_Ui_Phase(u32 phase_value) {
    static bool sStarted;

    if (phase_value > MODLOADER_EVENT_PHASE_POST) {
        return;
    }

    auto phase = static_cast<ModLoader_Event_Phase>(phase_value);
    if (ModLoader::Runtime::Imgui::Frame_Begin()) {
        if (phase == MODLOADER_EVENT_PHASE_PRE && !sStarted) {
            gComponentEvents.uiStart.Call_All();
            sStarted = true;
        }

        gComponentEvents.ui.Call(phase);
        if (phase == MODLOADER_EVENT_PHASE_NORMAL) {
            ModLoader::Runtime::Ui_Frames();
        }
        ModLoader::Runtime::Imgui::Frame_End();
    }

    if (phase == MODLOADER_EVENT_PHASE_POST) {
        ModLoader::Runtime::Rml_Frame_End();
        ModLoader::Runtime::Scratch_Reset();
    }
}

MODLOADER_EXPORT("modloader_ui_event_buffer") u64 ModLoader_Ui_Event_Buffer(u64 capacity) {
    return Reserve(&sUiEventBuffer, sUiDepth, capacity);
}

MODLOADER_EXPORT("modloader_ui_event") u32 ModLoader_Ui_Event(u64 listener, u64 size) {
    Buffer* buffer = At_Depth(&sUiEventBuffer, sUiDepth);
    if (buffer == nullptr || size > buffer->capacity) {
        return 0;
    }
    sUiDepth++;
    u32 result = ModLoader::Runtime::Rml_Event(listener, buffer->data, size);
    sUiDepth--;
    return result;
}

MODLOADER_EXPORT("modloader_event_buffer") u64 ModLoader_Event_Buffer(u64 capacity) {
    return Reserve(&sEventBuffer, sEventDepth, capacity);
}

MODLOADER_EXPORT("modloader_event_phase") void ModLoader_Event_Phase_Handle(u32 phase_value, u32 event, u64 size) {
    Buffer* buffer = At_Depth(&sEventBuffer, sEventDepth);

    if (phase_value > MODLOADER_EVENT_PHASE_POST || buffer == nullptr ||
        size < sizeof(ModLoader_Event_Header) || size > buffer->capacity) {
        return;
    }

    auto phase = static_cast<ModLoader_Event_Phase>(phase_value);
    bool normal = phase == MODLOADER_EVENT_PHASE_NORMAL;
    bool restoring = event == MODLOADER_EVENT_RESET || event == MODLOADER_EVENT_STATE_LOADED;
    sEventDepth++;
    if (normal) {
        ModLoader_Event_Header header;
        __builtin_memcpy(&header, buffer->data, sizeof(header));
        ModLoader::Runtime::Advance_Clock(MODLOADER_CLOCK_MILLISECONDS, header.milliseconds);
    }

    if (phase == MODLOADER_EVENT_PHASE_PRE && !restoring && ModLoader::Runtime::Platform_Maintain != nullptr) {
        ModLoader::Runtime::Platform_Maintain(event);
    }

    if (event == MODLOADER_EVENT_RESET) {
        gComponentEvents.reset.Call(phase);
    }
    else if (event == MODLOADER_EVENT_STATE_LOADED) {
        gComponentEvents.stateLoaded.Call(phase);
    }
    else if (event == MODLOADER_EVENT_PAUSE || event == MODLOADER_EVENT_RESUME || event == MODLOADER_EVENT_RESTART) {
        const auto& callbacks = event == MODLOADER_EVENT_PAUSE ? gComponentEvents.pause : event == MODLOADER_EVENT_RESUME ? gComponentEvents.resume : gComponentEvents.restart;
        callbacks.Call(phase);
        if (normal) {
            ModLoader::Runtime::Emulation_Event(event, buffer->data, size);
        }
    }
    else if (event == MODLOADER_EVENT_IMAGE_RESIZED) {
        if (size >= sizeof(ModLoader_Image_Resized_Record)) {
            ModLoader_Image_Resized_Record resized;
            __builtin_memcpy(&resized, buffer->data, sizeof(resized));
            gComponentEvents.imageResized.Call(phase, resized.size);
        }
        ModLoader::Runtime::Platform_Event(phase, event, buffer->data, size);
    }
    else if (event == MODLOADER_EVENT_LOBBY) {
        if (normal) {
            ModLoader::Runtime::Lobby_Event(buffer->data, size);
        }
    }
    else if (event == MODLOADER_EVENT_SERVER) {
        if (normal) {
            ModLoader::Runtime::Server_Event(buffer->data, size);
        }
    }
    else if (event == MODLOADER_EVENT_NET) {
        if (normal) {
            ModLoader::Runtime::Net_Event(buffer->data, size);
        }
    }
    else if (event == MODLOADER_EVENT_SETTING) {
        if (normal) {
            ModLoader::Runtime::Setting_Event(buffer->data, size);
        }
    }
    else if (event == MODLOADER_EVENT_TEXTURE_SOURCE) {
        if (normal) {
            ModLoader::Runtime::Textures_Event(buffer->data, size);
        }
    }
    else if (event == MODLOADER_EVENT_PAUSED) {
        // Only the clock above moves
    }
    else {
        ModLoader::Runtime::Platform_Event(phase, event, buffer->data, size);
    }

    if (phase == MODLOADER_EVENT_PHASE_PRE && restoring && ModLoader::Runtime::Platform_Maintain != nullptr) {
        ModLoader::Runtime::Platform_Maintain(event);
    }
    
    if (phase == MODLOADER_EVENT_PHASE_POST) {
        Finish_Event();
    }
    sEventDepth--;
}

// The host copied the platform's CPU state into the event buffer, and copies it back afterwards
MODLOADER_EXPORT("modloader_event_hypercall") u32 ModLoader_Event_Hypercall(u64 handler, u32 processor, u64 size) {
    Buffer* buffer = At_Depth(&sEventBuffer, sEventDepth);
    if (buffer == nullptr || size > buffer->capacity) {
        return 0;
    }
    sEventDepth++;
    bool handled = ModLoader::Runtime::Platform_Hypercall(handler, processor, buffer->data, size);

    Finish_Event();
    sEventDepth--;
    return handled ? 1 : 0;
}

// The same for a breakpoint's handler, with the hit after the CPU state; 1 pauses the game
MODLOADER_EXPORT("modloader_event_breakpoint") u32 ModLoader_Event_Breakpoint(u64 handler, u32 processor, u64 size) {
    Buffer* buffer = At_Depth(&sEventBuffer, sEventDepth);
    if (buffer == nullptr || size > buffer->capacity || buffer->capacity - size < sizeof(ModLoader_Break)) {
        return 0;
    }
    ModLoader_Break hit;
    __builtin_memcpy(&hit, buffer->data + size, sizeof(hit));
    sEventDepth++;
    bool pause = ModLoader::Runtime::Breakpoint_Event(handler, processor, buffer->data, size, hit);

    Finish_Event();
    sEventDepth--;
    return pause ? 1 : 0;
}

MODLOADER_EXPORT("modloader_dispatch") void ModLoader_Dispatch() {
    sEventDepth++;
    Finish_Event();
    sEventDepth--;
}

} // extern "C"


