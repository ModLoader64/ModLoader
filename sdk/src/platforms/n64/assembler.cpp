#include "../../internal.h"

#include <modloader/platforms/n64/assembler.h>
#include <modloader_bytes.h>

#include <cstring>

using namespace ModLoader;

std::expected<std::vector<u32>, std::string> N64::Assembler::Assemble(Guest::uptr origin, std::string_view source) {
    constexpr usize gErrorCapacity = 256;
    std::vector<u32> words;
    std::string error(gErrorCapacity, '\0');
    s64 size = ModLoader_Host_Asm_Assemble(origin, source.data(), source.size(), nullptr, 0, error.data(), error.size());

    if (size >= 0) {
        words.resize(static_cast<usize>(size) / 4);
        size = ModLoader_Host_Asm_Assemble(origin, source.data(), source.size(), words.data(), words.size() * 4,
            error.data(), error.size());
    }

    if (size < 0 || static_cast<usize>(size) != words.size() * 4) {
        error.resize(strnlen(error.data(), error.size()));
        return std::unexpected(error.empty() ? std::string("the code could not be assembled") : error);
    }

    for (u32& word : words) {
        word = Bytes::Read_Be32(&word);
    }

    return words;
}

std::string N64::Assembler::Disassemble(u32 instruction, Guest::uptr pc) {
    std::string text(64, '\0');
    u32 length = ModLoader_Host_Asm_Disassemble(instruction, pc, text.data(), text.size());

    if (length >= text.size()) {
        text.resize(static_cast<usize>(length) + 1);
        length = ModLoader_Host_Asm_Disassemble(instruction, pc, text.data(), text.size());
    }
    
    text.resize(length < text.size() ? length : text.size() - 1);
    return text;
}
