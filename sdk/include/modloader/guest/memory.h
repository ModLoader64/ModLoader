#pragma once

#include <modloader/guest/types.h>

namespace ModLoader::Guest {

// Copy guest bytes preserving byte order
bool Copy(uptr destination, uptr source, usize size);

// Marks code changed
void Invalidate_Code(uptr address, usize size);

// Read/write in game byte order, including TLB mappings and registers; returns bytes transferred
u64 Peek(uptr address, void* buffer, u64 size);
u64 Poke(uptr address, const void* data, u64 size); // Poke invalidates changed code

} // namespace ModLoader::Guest

