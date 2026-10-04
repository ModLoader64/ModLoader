#pragma once

#include <string_view>

namespace ModLoader::Detail {

#if defined(MODLOADER_MODULE_NAME)
constexpr std::string_view gModuleName = MODLOADER_MODULE_NAME;
#else
constexpr std::string_view gModuleName = "modloader-runtime";
#endif

#if defined(MODLOADER_LIBRARY_FOLDER)
constexpr std::string_view gModuleFolder = MODLOADER_LIBRARY_FOLDER;
#else
constexpr std::string_view gModuleFolder = {};
#endif

} // namespace ModLoader::Detail
