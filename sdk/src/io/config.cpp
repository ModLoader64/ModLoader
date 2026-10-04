#include "../internal.h"
#include "../listener_list.h"
#include "../read_text.h"

#include <modloader/detail/mounts.h>

#include <charconv>
#include <cstdlib>
#include <string>

namespace {

struct Setting_Change {
    std::string_view folder;
    std::string_view key;
};

[[clang::no_destroy]] ModLoader::Runtime::Listener_List<Setting_Change> sHandlers({MODLOADER_EVENT_SETTING});
} // namespace

bool ModLoader::Detail::Config_Declare(std::string_view folder, const Config::Setting& setting) {
    // The client reads terminated texts
    std::string key(setting.key);
    std::string label(setting.label);
    std::string help(setting.help);
    std::string choices(setting.choices);
    std::string value(setting.value);
    ModLoader_Module_Setting record = {};

    record.key = reinterpret_cast<u64>(key.c_str());
    record.label = reinterpret_cast<u64>(label.c_str());
    record.help = !help.empty() ? reinterpret_cast<u64>(help.c_str()) : 0;
    record.choices = !choices.empty() ? reinterpret_cast<u64>(choices.c_str()) : 0;
    record.value = reinterpret_cast<u64>(value.c_str());
    record.type = static_cast<u32>(setting.type);
    record.flags = setting.hidden ? MODLOADER_SETTING_HIDDEN : 0;
    record.minimum = setting.minimum;
    record.maximum = setting.maximum;
    return ModLoader_Host_Setting_Declare(folder.data(), folder.size(), &record) == 0;
}

std::optional<std::string> ModLoader::Detail::Config_Get(std::string_view folder, std::string_view key) {
    std::string terminated_key(key);
    return Runtime::Read_Text([&](char* buffer, u64 capacity) {
        return ModLoader_Host_Setting_Get(folder.data(), folder.size(), terminated_key.c_str(), buffer, capacity);
    });
}
f64 ModLoader::Detail::Config_Get_Float(std::string_view folder, std::string_view key) {
    std::optional<std::string> value = Config_Get(folder, key);

    return value ? strtod(value->c_str(), nullptr) : 0.0;
}

s64 ModLoader::Detail::Config_Get_Int(std::string_view folder, std::string_view key) {
    const auto text = Config_Get(folder, key);
    if (!text) {
        return 0;
    }
    s64 value = 0;
    const auto parsed = std::from_chars(text->data(), text->data() + text->size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text->data() + text->size() ? value : 0;
}

bool ModLoader::Detail::Config_Set(std::string_view folder, std::string_view key, std::string_view value) {
    std::string terminated_key(key);
    std::string terminated_value(value);

    return ModLoader_Host_Setting_Set(folder.data(), folder.size(), terminated_key.c_str(), terminated_value.c_str()) == 0;
}

u32 ModLoader::Detail::Config_On_Change(std::string_view folder, Function<void(std::string_view)> handler) {
    if (!handler) {
        return 0;
    }
    return sHandlers.Add([folder = std::string(folder), handler = std::move(handler)](const Setting_Change& change) {
        if (folder == change.folder) {
            handler(change.key);
        }
    });
}

void ModLoader::Config::Remove_On_Change(u32 handler) {
    sHandlers.Remove(handler);
}

ModLoader::Subscription ModLoader::Detail::Config_Subscribe(std::string_view folder, Function<void(std::string_view)> handler) {
    const u32 id = Config_On_Change(folder, std::move(handler));
    return sHandlers.Scope(id, [id] { Config::Remove_On_Change(id); });
}

void ModLoader::Runtime::Config_Shutdown() {
    sHandlers.Close();
}

void ModLoader::Runtime::Setting_Event(const void* record, u64 size) {
    ModLoader_Setting_Record change;
    if (size < sizeof(change)) {
        return;
    }
    __builtin_memcpy(&change, record, sizeof(change));
    change.owner[sizeof(change.owner) - 1] = '\0';
    change.key[sizeof(change.key) - 1] = '\0';
    sHandlers.Call({change.owner, change.key});
}
