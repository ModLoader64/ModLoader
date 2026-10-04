#pragma once

#include <modloader_platform.h>

namespace ModLoader {

// Runtime description for plugins that support multiple platforms
using Platform = ModLoader_Platform_Description;
// Filled before On_Prepare
extern const Platform& gPlatform;

} // namespace ModLoader

