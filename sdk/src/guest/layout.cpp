#include "../internal.h"
#include <modloader/guest/layout.h>

ModLoader::Guest::Layouts::Handle ModLoader::Guest::Layouts::Load_Json(std::string_view json) {
    return ModLoader_Host_Layout_Load(json.data(), json.size());
}

bool ModLoader::Guest::Layouts::Select(Handle profile) {
    return ModLoader_Host_Layout_Select(profile) != 0;
}

void ModLoader::Guest::Layouts::Unload(Handle profile) {
    ModLoader_Host_Layout_Unload(profile);
}
