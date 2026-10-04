#include "../internal.h"
#include <modloader/guest/memory.h>
#include <modloader/guest/symbols.h>

using namespace ModLoader;

void ModLoader::Symbols::Define(std::string_view name, Guest::uptr address) {
    ModLoader_Host_Symbol_Define(name.data(), name.size(), address);
}

Guest::uptr ModLoader::Symbols::Address(std::string_view name) {
    return static_cast<Guest::uptr>(ModLoader_Host_Symbol_Address(name.data(), name.size()));
}

void ModLoader::Guest::Invalidate_Code(Guest::uptr address, Guest::usize size) {
    ModLoader_Host_Invalidate_Code(address, size);
}

u64 ModLoader::Guest::Peek(Guest::uptr address, void* buffer, u64 size) {
    return ModLoader_Host_Memory_Peek(0, address, buffer, size);
}

u64 ModLoader::Guest::Poke(Guest::uptr address, const void* data, u64 size) {
    return ModLoader_Host_Memory_Poke(0, address, data, size);
}

bool ModLoader::Guest::Copy(Guest::uptr destination, Guest::uptr source, Guest::usize size) {
    return ModLoader_Host_Guest_Copy(destination, source, size) != 0;
}
