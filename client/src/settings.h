#pragma once

#include "base.h"
#include "base/json.h"
#include "modloader_settings.h"

#include <algorithm>

struct Setting {
    std::string key;
    std::string label;
    std::string page;
    std::string help;
    std::string choices;
    std::string value;
    u32 type = MODLOADER_SETTING_TEXT;
    u32 flags = 0; // MODLOADER_SETTING_*
    f64 minimum = 0.0;
    f64 maximum = 0.0;

    bool Is_Visible() const {
        return (flags & MODLOADER_SETTING_HIDDEN) == 0;
    }
};

struct Setting_Choice {
    std::string_view value;
    std::string_view label;
};

std::vector<Setting_Choice> Setting_Choices(std::string_view choices);

class Settings_Group;

using Settings_Changed = std::function<void(Settings_Group& group, const std::string& key, const std::string& value)>;

class Settings_Group {
public:
    // with_file stores values in <data>/config/<name>.json
    Settings_Group(std::string_view name, bool with_file, Settings_Changed changed);
    Settings_Group(const Settings_Group&) = delete;
    Settings_Group& operator=(const Settings_Group&) = delete;

    // Add or redescribe
    bool Add(const ModLoader_Setting& description);
    bool Set(std::string_view key, std::string_view value);
    std::optional<std::string> Get(std::string_view key) const;
    bool Get_Bool(std::string_view key) const;
    f64 Get_Number(std::string_view key) const;

    const std::string& Name() const {
        return name;
    }
    // Read with Settings_Lock held
    const std::vector<Setting>& Entries() const {
        return settings;
    }

private:
    auto* Find(this auto& group, std::string_view key) {
        auto found = std::ranges::find(group.settings, key, &Setting::key);
        return found != group.settings.end() ? &*found : nullptr;
    }
    void Store(const Setting& setting);

    std::string name;
    std::string path;
    Mutable_Json_Document document;
    std::vector<Setting> settings;
    Settings_Changed changed;
};

void Settings_Start(const std::string& data_directory);
void Settings_Stop();

// Groups retain insertion order and live until Settings_Stop.
Settings_Group* Settings_Group_Create(std::string_view name, bool with_file, Settings_Changed changed = nullptr);
Settings_Group* Settings_Group_Find_Or_Create(std::string_view name, Settings_Changed changed);

std::unique_lock<std::mutex> Settings_Lock();
// Caller holds Settings_Lock while using the returned collection.
const std::vector<std::unique_ptr<Settings_Group>>& Settings_Groups();
