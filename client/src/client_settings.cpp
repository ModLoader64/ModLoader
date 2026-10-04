#include "client_settings.h"

namespace {

constexpr ModLoader_Setting gClientSettings[] = {
    { "network.server_address", "Server address", "Network", "Hostname or IP address", nullptr, "", MODLOADER_SETTING_TEXT, MODLOADER_SETTING_RESTART, 0.0, 0.0 },
    { "network.server_port", "Server port", "Network", "Port", nullptr, "0", MODLOADER_SETTING_INT, MODLOADER_SETTING_RESTART, 0.0, 65535.0 },
    { "network.nickname", "Nickname", "Network", "What you call yourself", nullptr, "", MODLOADER_SETTING_TEXT, MODLOADER_SETTING_RESTART, 0.0, 0.0 },
    { "network.lobby", "Lobby", "Network", "The lobby to join ", nullptr, "", MODLOADER_SETTING_TEXT, MODLOADER_SETTING_RESTART, 0.0, 0.0 },
    { "network.password", "Lobby password", "Network", "", nullptr, "", MODLOADER_SETTING_TEXT, MODLOADER_SETTING_RESTART, 0.0, 0.0 },
    { "scaling",
      "Scaling",
      "Video",
      "How the image fills the window",
      "fit\tFit (its shape, whole)\nfill\tFill (its shape, cropped)\nstretch\tStretch\ninteger\tInteger (whole multiples)",
      "fit",
      MODLOADER_SETTING_CHOICE,
      0,
      0.0,
      0.0 },
    { "filter",
      "Filter",
      "Video",
      "The filter used to up or down-scale the image",
      "nearest\tNearest\nbilinear\tBilinear\nsharp_bilinear\tSharp "
      "bilinear\narea\tArea\nbicubic\tBicubic\nlanczos\tLanczos\nkaiser\tKaiser",
      "sharp_bilinear",
      MODLOADER_SETTING_CHOICE,
      0,
      0.0,
      0.0 },
    { "theme", "UI theme", "Video", "The colors of ModLoader's ImGui windows and menus", "dark\tDark\nlight\tLight", "dark", MODLOADER_SETTING_CHOICE, 0, 0.0, 0.0 },
    { "wasm_mode",
      "Plugin execution",
      "ModLoader",
      "Interpreter skips compilation",
      "interpreter\tInterpreter\nO0\tAOT O0\nO1\tAOT O1\nO2\tAOT O2\nO3\tAOT O3",
      "O0",
      MODLOADER_SETTING_CHOICE,
      MODLOADER_SETTING_RESTART,
      0.0,
      0.0 },
    { "hotkeys.pause", "Pause", "ModLoader/Hotkeys", "Pauses the game, or lets it go on", nullptr, "", MODLOADER_SETTING_BINDING, 0, 0.0, 0.0 },
    { "hotkeys.restart",
      "Restart",
      "ModLoader/Hotkeys",
      "Restarts the game, as the console's reset button does",
      nullptr,
      "",
      MODLOADER_SETTING_BINDING,
      0,
      0.0,
      0.0 },
    { "hotkeys.unthrottled", "Unthrottled", "ModLoader/Hotkeys", "Turns the speed limit off, or back on", nullptr, "", MODLOADER_SETTING_BINDING, 0, 0.0, 0.0 },
};

} // namespace

Client_Settings::Client_Settings(Settings_Changed changed)
    : group(Settings_Group_Create("client_settings", true, std::move(changed))) {
    for (const ModLoader_Setting& setting : gClientSettings) {
        group->Add(setting);
    }
}

std::string Client_Settings::Get(std::string_view key) const {
    return group->Get(key).value_or("");
}

Renderer_Scaling Client_Settings::Scaling() const {
    return Renderer_Scaling_From_Name(Get("scaling"));
}

Renderer_Filter Client_Settings::Filter() const {
    return Renderer_Filter_From_Name(Get("filter"));
}

Ui_Theme Client_Settings::Theme() const {
    return Ui_Theme_From_Name(Get("theme"));
}

u16 Client_Settings::Server_Port() const {
    return static_cast<u16>(group->Get_Number("network.server_port"));
}

Module_Execution Client_Settings::Execution_Mode() const {
    std::string mode = Get("wasm_mode");
    return mode == "interpreter" ? Module_Execution::Interpreter : static_cast<Module_Execution>(mode[1] - '0');
}
