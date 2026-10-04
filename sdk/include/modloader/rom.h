#pragma once

#include <modloader/types.h>

namespace ModLoader {

struct Rom_Info {
    char sha1[48];
    char title[32];
    char gameCode[8];
    u32 version;
    u64 size;
};

} // namespace ModLoader
