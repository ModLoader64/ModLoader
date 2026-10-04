#include "platform.h"
#include "modloader_events.h"
#include "runtime.h"
#include "sha1.h"

#include <string.h>
#include <unordered_map>

namespace {
std::unordered_map<std::string, Platform_Factory>& Factories() {
    static std::unordered_map<std::string, Platform_Factory> factories;
    return factories;
}
} // namespace

bool Platform::Load_Image(const std::string& path, Game_Image& image) {
    Sha1 hash;
    image.title = Path_File_Name(path.c_str());
    if (Directory_Exists(path)) {
        hash.Update(path.data(), path.size());
    }
    else {
        auto data = File_Read(path);
        if (!data) {
            Log_Error("content", "cannot read %s", path.c_str());
            return false;
        }
        image.data = std::move(*data);
        hash.Update(image.data.data(), image.data.size());
    }

    image.sha1 = hash.Hex_Digest();
    return true;
}

void Platform::Event_Record(u32, u64, const void* data, u64 size, std::vector<u8>& record) {
    record.clear();
    if (size > SIZE_MAX - sizeof(ModLoader_Event_Header) || (size != 0 && data == nullptr)) {
        return;
    }
    
    record.resize(sizeof(ModLoader_Event_Header) + size);
    if (size != 0) {
        memcpy(record.data() + sizeof(ModLoader_Event_Header), data, size);
    }
}

s64 Platform::Query(Runtime& runtime, u32 query, void* output, u64 capacity) {
    return runtime.Host().Platform_Query(query, output, capacity);
}

void Platform_Register(std::string_view identifier, Platform_Factory factory) {
    Factories().insert_or_assign(std::string(identifier), factory);
}

std::unique_ptr<Platform> Platform_Create(std::string_view identifier) {
    auto found = Factories().find(std::string(identifier));
    auto platform = found != Factories().end() ? found->second() : std::make_unique<Platform>();
    platform->identifier = identifier;
    return platform;
}
