#pragma once

// Common plugin API

#include <modloader/guest/memory.h>
#include <modloader/event.h>
#include <modloader/logger.h>
#include <modloader/memory.h>
#include <modloader/guest/machine.h>
#include <modloader/platform.h>
#include <modloader/size_literals.h>
#include <modloader/guest/symbols.h>
#include <modloader/text.h>
#include <modloader/types.h>

namespace ModLoader {

// False for dedicated servers
bool Game_Running();

} // namespace ModLoader

#include <modloader/lifecycle.h>

#include <stdlib.h>
#include <string.h>

