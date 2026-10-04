#include "module.h"

#include "modloader_platform.h"

#include <string.h>

namespace {

constexpr u64 gPageSize = 4096;

u64 Page_Run(u64 address, u64 remaining) {
    u64 run = gPageSize - (address & (gPageSize - 1));

    return run < remaining ? run : remaining;
}

} // namespace

bool Runtime::Map_Address(u64 address, u32& out_space, u64& out_offset, u32 processor) const {
    return processor < config.processors.size() && (platform.Map_Address(processor, address, out_space, out_offset) || Host().Translate_Address(processor, address, out_space, out_offset)) && out_space < config.spaces.size();
}

bool Runtime::Guest_Read(u64 address, void* out_data, u64 size) {
    if (size != 0 && (out_data == nullptr || size - 1 > UINT64_MAX - address)) {
        return false;
    }

    u8* bytes = static_cast<u8*>(out_data);
    u64 run;
    u32 space_index;
    u64 offset;

    for (u64 done = 0; done < size; done += run) {
        run = Page_Run(address + done, size - done);
        if (!Map_Address(address + done, space_index, offset)) {
            return false;
        }

        const ModLoader_Space_Descriptor& space = config.spaces[space_index];
        if (space.hostBase == nullptr || offset > space.usableSize || run > space.usableSize - offset) {
            return false;
        }

        if (space.codec == MODLOADER_CODEC_BIG_ENDIAN_WORD_SWAPPED) {
            for (u64 index = 0; index < run; index++) {
                bytes[done + index] = space.hostBase[(offset + index) ^ 3];
            }
        }
        else {
            memcpy(bytes + done, space.hostBase + offset, run);
        }
    }

    return true;
}

bool Runtime::Guest_Write(u64 address, const void* data, u64 size) {
    if (size != 0 && (data == nullptr || size - 1 > UINT64_MAX - address)) {
        return false;
    }

    const u8* bytes = static_cast<const u8*>(data);
    u64 run;
    u32 space_index;
    u64 offset;

    for (u64 done = 0; done < size; done += run) {
        run = Page_Run(address + done, size - done);
        if (!Map_Address(address + done, space_index, offset)) {
            return false;
        }

        const ModLoader_Space_Descriptor& space = config.spaces[space_index];
        if (space.hostBase == nullptr || offset > space.usableSize || run > space.usableSize - offset || (space.flags & MODLOADER_SPACE_WRITABLE) == 0) {
            return false;
        }

        if (space.codec == MODLOADER_CODEC_BIG_ENDIAN_WORD_SWAPPED) {
            for (u64 index = 0; index < run; index++) {
                space.hostBase[(offset + index) ^ 3] = bytes[done + index];
            }
        }
        else {
            memcpy(space.hostBase + offset, bytes + done, run);
        }
        Host().Invalidate_Code(space_index, offset, run);
    }

    return true;
}

void Runtime::Invalidate(u64 address, u64 size, u32 processor) {
    if (size != 0 && size - 1 > UINT64_MAX - address) {
        return;
    }
    
    u64 run;
    u32 space;
    u64 offset;

    for (u64 done = 0; done < size; done += run) {
        run = Page_Run(address + done, size - done);
        if (Map_Address(address + done, space, offset, processor)) {
            Host().Invalidate_Code(space, offset, run);
        }
    }
}
