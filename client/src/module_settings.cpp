// Changes from any thread dispatch at refresh.
#include "module.h"
#include "settings.h"

#include "modloader_settings.h"

namespace {

struct Setting_Change {
    std::string owner;
    std::string key;
};

std::vector<Setting_Change> sChanges; // under Settings_Lock

void Setting_Changed(Settings_Group& group, const std::string& key, const std::string&) {
    auto lock = Settings_Lock();

    for (const Setting_Change& change : sChanges) {
        if (change.owner == group.Name() && change.key == key) {
            return;
        }
    }

    sChanges.push_back({ group.Name(), key });
}

Settings_Group* Owner_Group(const Module& module, u64 folder_address, u64 folder_length) {
    std::optional<std::string> owner = module.Owner_Folder(folder_address, folder_length);
    return owner ? Settings_Group_Find_Or_Create(*owner, Setting_Changed) : nullptr;
}

bool Declare(const Module& module, u64 folder_address, u64 folder_length, u64 record_address) {
    Settings_Group* group = Owner_Group(module, folder_address, folder_length);
    const ModLoader_Module_Setting* record = module.Memory_As<ModLoader_Module_Setting>(record_address);
    ModLoader_Setting setting = {};
    std::string page;

    if (group == nullptr || record == nullptr || record->type == MODLOADER_SETTING_BINDING) {
        return false;
    }

    page = "Plugins/" + group->Name();
    setting.key = module.C_Text(record->key);
    setting.label = module.C_Text(record->label);
    setting.page = page.c_str();
    setting.help = module.C_Text(record->help);
    setting.choices = module.C_Text(record->choices);
    setting.value = module.C_Text(record->value);
    setting.type = record->type;
    setting.flags = record->flags & MODLOADER_SETTING_HIDDEN;
    setting.minimum = record->minimum;
    setting.maximum = record->maximum;

    return setting.key != nullptr && group->Add(setting);
}

s64 Get(const Module& module, u64 folder_address, u64 folder_length, u64 key_address, u64 buffer_address, u64 capacity) {
    Settings_Group* group = Owner_Group(module, folder_address, folder_length);
    const char* key = module.C_Text(key_address);
    char* buffer = capacity != 0 ? module.Memory_As<char>(buffer_address, capacity) : nullptr;
    std::optional<std::string> value = group != nullptr && key != nullptr && (buffer != nullptr || capacity == 0) ? group->Get(key) : std::nullopt;

    if (!value) {
        return -1;
    }

    if (buffer != nullptr) {
        Text_Copy({ buffer, capacity }, *value);
    }

    return static_cast<s64>(value->size());
}

NativeSymbol sNatives[] = {
    Native("setting_declare", "(III)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Declare(Module_Of(exec_env), slots[0], slots[1], slots[2]) ? 0 : static_cast<u64>(-1);
    }),
    Native("setting_get", "(IIIII)I", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = static_cast<u64>(Get(Module_Of(exec_env), slots[0], slots[1], slots[2], slots[3], slots[4]));
    }),
    Native("setting_set", "(IIII)i", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        Settings_Group* group = Owner_Group(module, slots[0], slots[1]);
        const char* key = module.C_Text(slots[2]);
        const char* value = module.C_Text(slots[3]);

        slots[0] = group != nullptr && key != nullptr && value != nullptr && group->Set(key, value) ? 0 : static_cast<u64>(-1);
    }),
};

} // namespace

void Module_Settings_Register_Natives() {
    Register_Natives(sNatives, "settings");
}

void Module_Settings_Dispatch(Runtime& runtime, u64 time) {
    std::vector<Setting_Change> changes;
    ModLoader_Setting_Record record = {};

    {
        auto lock = Settings_Lock();
        changes.swap(sChanges);
    }
    
    for (const Setting_Change& change : changes) {
        record.header = { time, Time_Monotonic_Milliseconds() };
        Text_Copy(record.key, change.key);
        Text_Copy(record.owner, change.owner);
        for (const std::unique_ptr<Module>& module : runtime.modules) {
            if (module->Manifest_For_Folder(change.owner) != nullptr) {
                module->Deliver(MODLOADER_EVENT_SETTING, &record, sizeof(record));
            }
        }
    }
}
