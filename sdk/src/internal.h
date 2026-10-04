#pragma once

#include "host_imports.h"

#include <string_view>

extern "C" {

int __cxa_thread_atexit_impl(void (*function)(void*), void* object, void* dso_handle);
void __modloader_run_thread_atexit();
void __modloader_run_atexit();

} // extern "C"

namespace ModLoader::Runtime {

bool Follow_Event(u32 event, bool follow);

u32 Rml_Event(u64 listener, const u8* buffer, u64 size);
void Rml_Frame_End();

void Scratch_Reset();
void Scratch_Release();

extern u32 gSession;
void Lobby_Event(const void* record, u64 size);
void Server_Event(const void* record, u64 size);
void Net_Event(const void* record, u64 size);
void Net_Shutdown();
void Textures_Event(const void* record, u64 size);
void Textures_Shutdown();
void Setting_Event(const void* record, u64 size);
void Emulation_Event(u32 event, const void* record, u64 size);
void Ui_Frames();
void Ui_Shutdown();
void Config_Shutdown();
void Listeners_Shutdown();
void Advance_Clock(u64 clock, u64 now);
void Timers_Shutdown();
void Dispatch();
void Promises_Shutdown();
void Stop_Tasks();
void Breakpoints_Shutdown();
bool Breakpoint_Event(u64 handler, u32 processor, void* state, u64 size, const ModLoader_Break& hit);

bool Platform_Initialize(const ModLoader_Platform_Description* description);
void Platform_Start();
__attribute__((weak)) void Platform_Shutdown();
__attribute__((weak)) void Platform_Maintain(u32 event);
void Platform_Event(ModLoader_Event_Phase phase, u32 event, const void* record, u64 size);
bool Platform_Clock_Event(u32 clock, u32* event);
bool Platform_Hypercall(u64 handler, u32 processor, void* state, u64 size);

} // namespace ModLoader::Runtime

