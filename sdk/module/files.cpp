#include <modloader/detail/identity.h>
#include <modloader/detail/mounts.h>

namespace ModLoader::File {

Handle Open(std::string_view path, Mode mode) {
    return Detail::File_Open(Detail::gModuleFolder, path, mode);
}

bool Delete(std::string_view path) {
    return Detail::File_Delete(Detail::gModuleFolder, path);
}

bool Exists(std::string_view path) {
    return Detail::File_Exists(Detail::gModuleFolder, path);
}

bool Create_Directory(std::string_view path) {
    return Detail::File_Create_Directory(Detail::gModuleFolder, path);
}

bool List(std::string_view path, Function<void(std::string_view, bool)> each) {
    return Detail::File_List(Detail::gModuleFolder, path, std::move(each));
}

std::optional<std::vector<u8>> Read_All(std::string_view path) {
    return Detail::File_Read_All(Detail::gModuleFolder, path);
}

bool Write_All(std::string_view path, const void* data, u64 size) {
    return Detail::File_Write_All(Detail::gModuleFolder, path, data, size);
}

Promise<std::vector<u8>> Read_All_Async(std::string_view path) {
    return Detail::File_Read_All_Async(Detail::gModuleFolder, path);
}

Promise<bool> Write_All_Async(std::string_view path, const void* data, u64 size) {
    return Detail::File_Write_All_Async(Detail::gModuleFolder, path, data, size);
}

} // namespace ModLoader::File

namespace ModLoader::Config {

bool Declare(const Setting& setting) {
    return Detail::Config_Declare(Detail::gModuleFolder, setting);
}

std::optional<std::string> Get(std::string_view key) {
    return Detail::Config_Get(Detail::gModuleFolder, key);
}

s64 Get_Int(std::string_view key) {
    return Detail::Config_Get_Int(Detail::gModuleFolder, key);
}

f64 Get_Float(std::string_view key) {
    return Detail::Config_Get_Float(Detail::gModuleFolder, key);
}

bool Set(std::string_view key, std::string_view value) {
    return Detail::Config_Set(Detail::gModuleFolder, key, value);
}

u32 On_Change(Function<void(std::string_view)> handler) {
    return Detail::Config_On_Change(Detail::gModuleFolder, std::move(handler));
}

Subscription Subscribe(Function<void(std::string_view)> handler) {
    return Detail::Config_Subscribe(Detail::gModuleFolder, std::move(handler));
}

} // namespace ModLoader::Config

namespace ModLoader::Json {

Document Load(std::string_view path) {
    return Detail::Json_Load(Detail::gModuleFolder, path);
}

bool Save(std::string_view path, const Document& document) {
    return Detail::Json_Save(Detail::gModuleFolder, path, document);
}

} // namespace ModLoader::Json
