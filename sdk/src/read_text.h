#pragma once

#include <modloader/types.h>

#include <optional>
#include <string>

namespace ModLoader::Runtime {

template<typename Read>
std::optional<std::string> Read_Text(Read read) {
    std::string text;
    s64 size = read(nullptr, 0);

    for (u32 attempt = 0; attempt < 4 && size >= 0; attempt++) {
        text.resize(static_cast<usize>(size) + 1);
        size = read(text.data(), text.size());
        if (size >= 0 && static_cast<u64>(size) < text.size()) {
            text.resize(static_cast<usize>(size));
            return text;
        }
    }
    return std::nullopt;
}

} // namespace ModLoader::Runtime
