#pragma once

#include <modloader/io/file.h>
#include <modloader/types.h>

namespace ModLoader::Guest::Layouts {

using Handle = u32;

Handle Load_Json(std::string_view json);

inline Handle Load(std::string_view path) {
    auto bytes = File::Read_All(path);
    if (!bytes) {
        return 0;
    }
    return Load_Json({ reinterpret_cast<const char*>(bytes->data()), bytes->size() });
}

// Zero restores the declared layout
bool Select(Handle profile);
void Unload(Handle profile);

} // namespace ModLoader::Guest::Layouts
