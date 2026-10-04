#include "game_session.h"

#include "settings.h"

void Game_Session::Bind_Control(std::string_view key, std::string_view value) {
    if (!key.starts_with("controller1.")) {
        return;
    }
    key.remove_prefix(12);
    for (u32 index = 0; index < platform->controls.size(); index++) {
        if (key == platform->controls[index]) {
            presenter->Bind(index, value);
        }
    }
}

void Game_Session::Adapter_Setting_Changed(const std::string& key, const std::string& value) {
    Bind_Control(key, value);
    auto lock = Settings_Lock();
    std::erase_if(pendingSettings, [&](const std::pair<std::string, std::string>& pending) {
        return pending.first == key;
    });
    pendingSettings.emplace_back(key, value);
    settingsPending = true;
}

void Game_Session::Describe_Adapter_Settings(bool bind) {
    ModLoader_Setting setting;
    u32 count = adapter->settingCount != nullptr && adapter->settingDescribe != nullptr && adapter->settingSet != nullptr ? adapter->settingCount(handle) : 0;

    for (u32 index = 0; index < count; index++) {
        if (adapter->settingDescribe(handle, index, &setting) != 0 || !adapterSettings->Add(setting)) {
            continue;
        }
        if (bind && setting.type == MODLOADER_SETTING_BINDING) {
            Bind_Control(setting.key, setting.value);
        }
        if ((setting.flags & MODLOADER_SETTING_SPEED_LIMIT) != 0) {
            presenter->Set_Audio_Speed_Limit(std::string_view(setting.value) == "true");
        }
    }
}

void Game_Session::Apply_Adapter_Settings() {
    std::vector<std::pair<std::string, std::string>> pending;

    if (!settingsPending.exchange(false)) {
        return;
    }

    {
        auto lock = Settings_Lock();
        pending.swap(pendingSettings);
    }
    
    for (const auto& [key, value] : pending) {
        if (adapter->settingSet == nullptr || adapter->settingSet(handle, key.c_str(), value.c_str()) != 0) {
            Log_Warning("adapter", "rejected setting %s", key.c_str());
        }
    }
    Describe_Adapter_Settings(false);
}

void Game_Session::Load_Adapter_Settings() {
    adapterSettings = Settings_Group_Create(adapter->adapterIdentifier, false, [this](Settings_Group&, const std::string& key, const std::string& value) {
        Adapter_Setting_Changed(key, value);
    });
    presenter->Set_Controls(*platform);
    Describe_Adapter_Settings(true);
}
