#pragma once

#include <modloader/guest/types.h>
#include <instruction.h>

namespace ModLoader::N64::Detail {

constexpr u32 gMaxRelocatedWords = 16;

u32 Relocate_Detour(const u32 original[3], Guest::uptr site, Guest::uptr destination, u32 out_words[gMaxRelocatedWords]);
bool Preserved_Detour_Words(const u32 original[3], Guest::uptr site, Guest::uptr destination, u32 out_words[2]);
bool Delay_Slot_Valid(u32 word, Guest::uptr address);
bool Payload_Valid(const u8* bytes, Guest::usize size, Guest::uptr origin);
bool Detour_Words(Guest::uptr site, Guest::uptr destination, u32 out_words[2], u32 delay_word = ::N64::gNop);

} // namespace ModLoader::N64::Detail
