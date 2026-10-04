#pragma once

#include <modloader/platforms/n64/platform.h>
#include <modloader/detail/component.h>

namespace ModLoader::Runtime {

// Filled by the generated module entry
struct N64_Component {
#define MODLOADER_LIFECYCLE(field, arguments, pre, normal, post, event) Event_Callbacks<void arguments> field;
#include <modloader/platforms/n64/lifecycle.def>
#undef MODLOADER_LIFECYCLE
};

} // namespace ModLoader::Runtime

