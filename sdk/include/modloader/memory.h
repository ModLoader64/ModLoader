#pragma once

#include <modloader/types.h>
#include <modloader_bytes.h>

namespace ModLoader::Memory {

// do not free or retain the pointer; alignment is a power of two (minimum 16); returns nullptr on allocation failure
void* Scratch_Alloc(usize size, usize alignment = 16);

// Round up to a power-of-two alignment
constexpr u64 Align_Up(u64 value, u64 alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

using Bytes::Read_Be16;
using Bytes::Read_Be32;
using Bytes::Read_Le32;
using Bytes::Write_Be16;
using Bytes::Write_Be32;

} // namespace ModLoader::Memory
