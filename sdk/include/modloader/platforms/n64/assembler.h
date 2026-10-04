#pragma once

#include <modloader/guest/types.h>

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace ModLoader::N64::Assembler {

// GNU MIPS syntax; resolves Symbols::Define names
// Returns numeric instruction words, or an error message
// Origin is the CPU address where the code will execute
std::expected<std::vector<u32>, std::string> Assemble(Guest::uptr origin, std::string_view source);

std::string Disassemble(u32 instruction, Guest::uptr pc);

} // namespace ModLoader::N64::Assembler
