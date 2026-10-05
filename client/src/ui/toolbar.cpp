#include "ui/toolbar.h"
#include "ui/textures.h"
#include "settings.h"
#include "imgui.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <math.h>
#include <algorithm>

namespace {

constexpr s16 gCaptureAxis = 24000;
constexpr const char* gMenuOrder[] = { "Video", "Input", "Emulation", "Plugins" };
// Indexed by SDL_GamepadButton
constexpr const char* gPadNames[] = {
    "south", "east", "west", "north", "back", "guide", "start", "leftstick",
    "rightstick", "leftshoulder", "rightshoulder", "dpup", "dpdown", "dpleft", "dpright"
};

struct Change {
    Settings_Group* group;
    std::string key;
    std::string value;
};

std::vector<Change> sChanges;

Settings_Group* sCaptureGroup;
std::string sCaptureKey;
std::string sCaptureValue;

ImGuiID sEditId;
f64 sEditNumber;
s64 sEditInteger;
char sEditText[512];

std::vector<std::string_view> Binding_Parts(std::string_view binding) {
    std::vector<std::string_view> parts;
    usize end;

    while (!binding.empty()) {
        end = binding.find('|');
        parts.push_back(binding.substr(0, end));
        binding.remove_prefix(end != std::string_view::npos ? end + 1 : binding.size());
    }

    return parts;
}

std::string Describe_Binding(std::string_view binding) {
    std::string text;
    bool pad = false;

    for (std::string_view part : Binding_Parts(binding)) {
        text += text.empty() ? "" : ", ";
        if (!part.starts_with("key:") && !pad) {
            text += "Pad ";
            pad = true;
        }
        text += part.substr(part.find(':') != std::string_view::npos ? part.find(':') + 1 : 0);
    }

    return !text.empty() ? text : "(none)";
}

const char* Shortcut(const std::string& binding, std::string& text) {
    text = Describe_Binding(binding);
    return !binding.empty() ? text.c_str() : nullptr;
}

std::string Merge_Binding(std::string_view binding, std::string_view input) {
    bool key = input.starts_with("key:");
    std::string text(key ? input : "");

    for (std::string_view part : Binding_Parts(binding)) {
        if (part.starts_with("key:") != key) {
            text += text.empty() ? "" : "|";
            text += part;
        }
    }

    if (!key) {
        text += text.empty() ? "" : "|";
        text += input;
    }

    return text;
}

void Draw_Tooltip(const Setting& setting) {
    bool restart = (setting.flags & MODLOADER_SETTING_RESTART) != 0;

    if ((!setting.help.empty() || restart) && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::SetTooltip(
            "%s%s%s",
            setting.help.c_str(),
            !setting.help.empty() && restart ? "\n" : "",
            restart ? "Requires restart" : ""
        );
    }
}

s64 Integer_Bound(f64 number) {
    if (isnan(number)) {
        return 0;
    }

    if (number <= static_cast<f64>(INT64_MIN)) {
        return INT64_MIN;
    }

    if (number >= static_cast<f64>(INT64_MAX)) {
        return INT64_MAX;
    }

    return static_cast<s64>(number);
}

void Draw_Number(Settings_Group& group, const Setting& setting) {
    ImGuiID id = ImGui::GetID("number");
    bool bounded = setting.minimum < setting.maximum;
    f64 number = sEditId == id ? sEditNumber : strtod(setting.value.c_str(), nullptr);
    s64 whole = sEditId == id ? sEditInteger : strtoll(setting.value.c_str(), nullptr, 10);
    s64 whole_minimum = Integer_Bound(setting.minimum);
    s64 whole_maximum = Integer_Bound(setting.maximum);

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    if (setting.type == MODLOADER_SETTING_INT && bounded) {
        ImGui::SliderScalar("##number", ImGuiDataType_S64, &whole, &whole_minimum, &whole_maximum, nullptr, ImGuiSliderFlags_AlwaysClamp);
    }
    else if (setting.type == MODLOADER_SETTING_INT) {
        ImGui::InputScalar("##number", ImGuiDataType_S64, &whole);
    }
    else if (bounded) {
        ImGui::SliderScalar("##number", ImGuiDataType_Double, &number, &setting.minimum, &setting.maximum, "%.3g", ImGuiSliderFlags_AlwaysClamp);
    }
    else {
        ImGui::InputDouble("##number", &number);
    }

    if (ImGui::IsItemActive()) {
        sEditId = id;
        sEditNumber = number;
        sEditInteger = whole;
    }

    if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::string value = setting.type == MODLOADER_SETTING_INT ? Text_Format("%lld", static_cast<long long>(whole)) : Text_Format("%.17g", number);
        sChanges.push_back({ &group, setting.key, std::move(value) });
    }

    if (!ImGui::IsItemActive() && sEditId == id) {
        sEditId = 0;
    }

    Draw_Tooltip(setting);
    ImGui::SameLine();
    ImGui::TextUnformatted(setting.label.c_str());
}

void Draw_Choice(Settings_Group& group, const Setting& setting) {
    std::vector<Setting_Choice> choices = Setting_Choices(setting.choices);
    std::string_view shown = setting.value;
    std::string label;

    for (const Setting_Choice& choice : choices) {
        shown = choice.value == setting.value ? choice.label : shown;
    }

    label = setting.label + ": " + std::string(shown) + "###choice";
    if (ImGui::BeginMenu(label.c_str())) {
        for (const Setting_Choice& choice : choices) {
            label = choice.label;
            if (ImGui::MenuItem(label.c_str(), nullptr, choice.value == setting.value)) {
                sChanges.push_back({ &group, setting.key, std::string(choice.value) });
            }
        }
        ImGui::EndMenu();
    }

    Draw_Tooltip(setting);
}

void Draw_Binding(Settings_Group& group, const Setting& setting) {
    bool capturing = sCaptureGroup == &group && sCaptureKey == setting.key;
    std::string shown = capturing ? "Press a key or button (Esc: cancel)" : Describe_Binding(setting.value);

    ImGui::TextUnformatted(setting.label.c_str());
    ImGui::SameLine(ImGui::GetFontSize() * 7.0f);
    if (ImGui::Button(shown.c_str(), ImVec2(ImGui::GetFontSize() * 20.0f, 0.0f))) {
        sCaptureGroup = &group;
        sCaptureKey = setting.key;
        sCaptureValue = setting.value;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        sCaptureGroup = capturing ? nullptr : sCaptureGroup;
        sChanges.push_back({ &group, setting.key, "" });
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !capturing) {
        ImGui::SetTooltip("Click, then press a key or a gamepad button or stick; each replaces its kind. Right-click clears");
    }
}

void Draw_Text(Settings_Group& group, const Setting& setting) {
    ImGuiID id = ImGui::GetID("text");

    if (sEditId != id) {
        Text_Copy(sEditText, setting.value);
    }

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    ImGui::InputText("##text", sEditText, sizeof(sEditText));
    sEditId = ImGui::IsItemActive() ? id : (sEditId == id ? 0 : sEditId);

    if (ImGui::IsItemDeactivatedAfterEdit()) {
        sChanges.push_back({ &group, setting.key, sEditText });
    }

    Draw_Tooltip(setting);
    ImGui::SameLine();
    ImGui::TextUnformatted(setting.label.c_str());
}

void Draw_Setting(Settings_Group& group, const Setting& setting) {
    ImGui::PushID(&group);
    ImGui::PushID(setting.key.c_str());
    ImGui::BeginDisabled((setting.flags & MODLOADER_SETTING_DISABLED) != 0);
    switch (setting.type) {
    case MODLOADER_SETTING_BOOL:
        if (ImGui::MenuItem(setting.label.c_str(), nullptr, setting.value == "true")) {
            sChanges.push_back({ &group, setting.key, setting.value == "true" ? "false" : "true" });
        }
        Draw_Tooltip(setting);
        break;
    case MODLOADER_SETTING_INT:
    case MODLOADER_SETTING_FLOAT:
        Draw_Number(group, setting);
        break;
    case MODLOADER_SETTING_CHOICE:
        Draw_Choice(group, setting);
        break;
    case MODLOADER_SETTING_BINDING:
        Draw_Binding(group, setting);
        break;
    default:
        Draw_Text(group, setting);
        break;
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    ImGui::PopID();
}

bool Page_Within(std::string_view page, std::string_view path) {
    return page.starts_with(path) && (page.size() == path.size() || page[path.size()] == '/');
}

bool Page_Has(std::string_view path) {
    for (const std::unique_ptr<Settings_Group>& group : Settings_Groups()) {
        for (const Setting& setting : group->Entries()) {
            if (setting.Is_Visible() && Page_Within(setting.page, path)) {
                return true;
            }
        }
    }
    return false;
}

void Draw_Page(const std::string& path) {
    std::vector<std::string> children;
    std::string_view child;

    ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
    for (const std::unique_ptr<Settings_Group>& group : Settings_Groups()) {
        for (const Setting& setting : group->Entries()) {
            if (!setting.Is_Visible() || !Page_Within(setting.page, path)) {
                continue;
            }

            if (setting.page == path) {
                Draw_Setting(*group, setting);
                continue;
            }

            child = std::string_view(setting.page).substr(path.size() + 1);
            child = child.substr(0, child.find('/'));
            if (std::find(children.begin(), children.end(), child) == children.end()) {
                children.emplace_back(child);
            }
        }
    }

    ImGui::PopItemFlag();
    for (const std::string& name : children) {
        if (ImGui::BeginMenu(name.c_str())) {
            Draw_Page(path + "/" + name);
            ImGui::EndMenu();
        }
    }
}

std::vector<std::string> Other_Menus() {
    std::vector<std::string> menus;
    std::string menu;

    for (const std::unique_ptr<Settings_Group>& group : Settings_Groups()) {
        for (const Setting& setting : group->Entries()) {
            menu = setting.page.substr(0, setting.page.find('/'));
            if (!menu.empty() && menu != "ModLoader" && std::find(std::begin(gMenuOrder), std::end(gMenuOrder), menu) == std::end(gMenuOrder) &&
                std::find(menus.begin(), menus.end(), menu) == menus.end()) {
                menus.push_back(menu);
            }
        }
    }

    return menus;
}

} // namespace

Toolbar_Action Toolbar_Build(const Toolbar_State& state) {
    Toolbar_Action action = Toolbar_Action_None;
    std::vector<Change> changes;
    std::string shortcut;

    if (!ImGui::BeginMainMenuBar()) {
        return action;
    }

    {
        std::unique_lock lock = Settings_Lock();

        if (ImGui::BeginMenu("ModLoader")) {
            action = ImGui::MenuItem(state.paused ? "Resume" : "Pause", Shortcut(state.pauseBinding, shortcut)) ? Toolbar_Action_Pause : action;
            action = ImGui::MenuItem("Restart", Shortcut(state.restartBinding, shortcut)) ? Toolbar_Action_Restart : action;
            if (state.savestates) {
                ImGui::Separator();
                action = ImGui::MenuItem("Save state", "F5") ? Toolbar_Action_Save_State : action;
                action = ImGui::MenuItem("Load state", "F7") ? Toolbar_Action_Load_State : action;
            }

            if (Page_Has("ModLoader")) {
                ImGui::Separator();
                Draw_Page("ModLoader");
            }

            ImGui::Separator();
            action = ImGui::MenuItem("Quit") ? Toolbar_Action_Quit : action;
            ImGui::EndMenu();
        }

        for (const char* menu : gMenuOrder) {
            bool video = std::string_view(menu) == "Video";
            bool settings = Page_Has(menu);

            if ((video || settings) && ImGui::BeginMenu(menu)) {
                Draw_Page(menu);
                if (video) {
                    if (settings) {
                        ImGui::Separator();
                    }
                    Textures_Menu();
                }
                ImGui::EndMenu();
            }
        }

        for (const std::string& menu : Other_Menus()) {
            if (ImGui::BeginMenu(menu.c_str())) {
                Draw_Page(menu);
                ImGui::EndMenu();
            }
        }
    }

    ImGui::EndMainMenuBar();
    Textures_Build(state.textures);
    changes.swap(sChanges);
    for (const Change& change : changes) {
        change.group->Set(change.key, change.value);
    }

    return action;
}

bool Toolbar_Capture_Event(const SDL_Event& event) {
    std::string input;
    Settings_Group* group;
    const char* name;

    if (sCaptureGroup == nullptr) {
        return false;
    }
    
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
        if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
            sCaptureGroup = nullptr;
            return true;
        }
        name = SDL_GetScancodeName(event.key.scancode);
        if (!event.key.repeat && name != nullptr && name[0] != '\0') {
            input = std::string("key:") + name;
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (event.gbutton.button < std::size(gPadNames)) {
            input = std::string("pad:") + gPadNames[event.gbutton.button];
        }
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        name = SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(event.gaxis.axis));
        if ((event.gaxis.value >= gCaptureAxis || event.gaxis.value <= -gCaptureAxis) && name != nullptr) {
            input = Text_Format("axis:%s%c", name, event.gaxis.value > 0 ? '+' : '-');
        }
        break;
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        break;
    default:
        return false;
    }
    if (!input.empty()) {
        group = sCaptureGroup;
        sCaptureGroup = nullptr;
        group->Set(sCaptureKey, Merge_Binding(sCaptureValue, input));
    }
    return true;
}

bool Toolbar_Capturing() {
    return sCaptureGroup != nullptr;
}

s32 Binding_Pad_Button(std::string_view name) {
    for (u32 index = 0; index < std::size(gPadNames); index++) {
        if (name == gPadNames[index]) {
            return static_cast<s32>(index);
        }
    }
    return -1;
}
