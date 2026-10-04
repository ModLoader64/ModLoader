#pragma once

#include <modloader/guest/types.h>
#include <string_view>

namespace ModLoader::Symbols {

// Named guest CPU addresses for imported externs and assembly
// Define in On_Prepare; binding precedes static constructors
void Define(std::string_view name, Guest::uptr address);

// CPU address, or 0 if undefined
Guest::uptr Address(std::string_view name);

} // namespace ModLoader::Symbols

