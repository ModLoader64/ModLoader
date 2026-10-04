#include <modloader/detail/identity.h>
#include <modloader/detail/mounts.h>
#include <modloader/graphics/textures.h>
#include <modloader/logger.h>

namespace ModLoader::Rml {

Document Load_Document(const Ui::Window& window, std::string_view rml) {
    return Detail::Rml_Load_Document(Detail::gModuleFolder, window, rml);
}

Document Load_Document_File(const Ui::Window& window, std::string_view path) {
    return Detail::Rml_Load_Document_File(Detail::gModuleFolder, window, path);
}

bool Load_Font(std::string_view path, bool fallback) {
    return Detail::Rml_Load_Font(Detail::gModuleFolder, path, fallback);
}

} // namespace ModLoader::Rml

bool ModLoader::Textures::Source::Create(std::string_view id, std::string_view name, std::string_view path, bool enabled) {
    return Create_In(Detail::gModuleFolder, id, name, path, enabled);
}

ImFont* ImFontAtlas::AddFontFromFileTTF(const char* filename, float size_pixels, const ImFontConfig* font_config, const ImWchar*) {
    auto font = ModLoader::Detail::Imgui_Load_Font(ModLoader::Detail::gModuleFolder, filename, size_pixels, font_config);
    if (font == nullptr) {
        ModLoader::Logger::Error("ImGui font load failed: %s", filename);
    }
    return font;
}
