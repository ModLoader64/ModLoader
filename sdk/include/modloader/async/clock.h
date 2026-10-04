#pragma once

#include <modloader/types.h>
#include <modloader_events.h>

namespace ModLoader {

// Timer units. Platforms may define additional clocks (e.g. N64::Clocks::VI)
enum class Clock : u64 {
    Milliseconds = MODLOADER_CLOCK_MILLISECONDS, // Host monotonic time
};

} // namespace ModLoader
