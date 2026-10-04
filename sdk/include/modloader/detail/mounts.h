#pragma once

#include <modloader/io/file.h>
#include <modloader/io/config.h>
#include <modloader/io/json.h>
#include <modloader/ui/rml.h>

namespace ModLoader::Detail {

File::Handle File_Open(std::string_view folder, std::string_view path, File::Mode mode);
bool File_Delete(std::string_view folder, std::string_view path);
bool File_Exists(std::string_view folder, std::string_view path);
bool File_Create_Directory(std::string_view folder, std::string_view path);
bool File_List(std::string_view folder, std::string_view path, Function<void(std::string_view, bool)> each);
std::optional<std::vector<u8>> File_Read_All(std::string_view folder, std::string_view path);
bool File_Write_All(std::string_view folder, std::string_view path, const void* data, u64 size);
Promise<std::vector<u8>> File_Read_All_Async(std::string_view folder, std::string_view path);
Promise<bool> File_Write_All_Async(std::string_view folder, std::string_view path, const void* data, u64 size);

bool Config_Declare(std::string_view folder, const Config::Setting& setting);
std::optional<std::string> Config_Get(std::string_view folder, std::string_view key);
f64 Config_Get_Float(std::string_view folder, std::string_view key);
s64 Config_Get_Int(std::string_view folder, std::string_view key);
bool Config_Set(std::string_view folder, std::string_view key, std::string_view value);
u32 Config_On_Change(std::string_view folder, Function<void(std::string_view)> handler);
Subscription Config_Subscribe(std::string_view folder, Function<void(std::string_view)> handler);

Json::Document Json_Load(std::string_view folder, std::string_view path);
bool Json_Save(std::string_view folder, std::string_view path, const Json::Document& document);

Rml::Document Rml_Load_Document(std::string_view folder, const Ui::Window& window, std::string_view rml);
Rml::Document Rml_Load_Document_File(std::string_view folder, const Ui::Window& window, std::string_view path);
bool Rml_Load_Font(std::string_view folder, std::string_view path, bool fallback);
ImFont* Imgui_Load_Font(std::string_view folder, const char* filename, float size_pixels, const ImFontConfig* font_config);

} // namespace ModLoader::Detail
