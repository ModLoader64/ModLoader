#pragma once

#include <modloader/platforms/n64/cpu.h>

namespace ModLoader::N64::Detail {

u64 Add_Callback(Hypercall::Callback callback);
void Remove_Callback(u64 token);
bool Callback_Active(u64 token);
bool Callbacks_Stopping();

} // namespace ModLoader::N64::Detail

namespace ModLoader::Runtime {
void Patches_Shutdown();
} // namespace ModLoader::Runtime
