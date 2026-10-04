#include "../../internal.h"
#include <modloader/platform.h>

namespace {
ModLoader::Platform sPlatform;
}

const ModLoader::Platform& ModLoader::gPlatform = sPlatform;

bool ModLoader::Runtime::Platform_Initialize(const ModLoader_Platform_Description* description) {
    sPlatform = *description;
    return true;
}

void ModLoader::Runtime::Platform_Start() {
}

void ModLoader::Runtime::Platform_Event(ModLoader_Event_Phase, u32, const void*, u64) {
}

bool ModLoader::Runtime::Platform_Clock_Event(u32, u32*) {
    return false;
}

bool ModLoader::Runtime::Platform_Hypercall(u64, u32, void*, u64) {
    return false;
}

