#include "ui/toolbar.h"
#include "ui/textures.h"
#include "settings.h"
#include "imgui.h"
#include "imgui_internal.h"
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
u32 sCaptureMouseButtons;
u32 sCaptureIgnoredMouse;
SDL_Keymod sCaptureMouseModifiers;
constexpr const char* gMouseNames[] = { "left", "middle", "right", "side1", "side2" };

ImGuiID sEditId;
f64 sEditNumber;
s64 sEditInteger;
char sEditText[512];

std::string_view Take_Binding_Part(std::string_view& binding, char separator = '|') {
    usize end = binding.find(separator);
    std::string_view part = binding.substr(0, end);
    binding.remove_prefix(end != std::string_view::npos ? end + 1 : binding.size());
    return part;
}

std::string Describe_Binding(std::string_view binding) {
    std::string text;
    while (!binding.empty()) {
        std::string_view alternative = Take_Binding_Part(binding);
        text += text.empty() ? "" : ", ";
        bool first = true;
        while (!alternative.empty()) {
            std::string_view part = Take_Binding_Part(alternative, '&');
            if (!first) {
                text += " + ";
            }

            first = false;
            if (part.starts_with("mouse:")) {
                text += "Mouse ";
            }
            else if (part.starts_with("pad:") || part.starts_with("axis:")) {
                text += "Pad ";
            }

            usize separator = part.find(':');
            text += part.substr(separator != std::string_view::npos ? separator + 1 : 0);
        }
    }
    return !text.empty() ? text : "(none)";
}

const char* Shortcut(const std::string& binding, std::string& text) {
    text = Describe_Binding(binding);
    return !binding.empty() ? text.c_str() : nullptr;
}

enum class Binding_Family {
    Keyboard,
    Controller,
    Mouse,
};

Binding_Family Get_Binding_Family(std::string_view binding) {
    if (binding.starts_with("mouse:") || binding.find("&mouse:") != std::string_view::npos) {
        return Binding_Family::Mouse;
    }

    if (binding.starts_with("pad:") || binding.starts_with("axis:") ||
        binding.find("&pad:") != std::string_view::npos || binding.find("&axis:") != std::string_view::npos) {
        return Binding_Family::Controller;
    }
    return Binding_Family::Keyboard;
}

std::string Merge_Binding(std::string_view binding, std::string_view input) {
    const Binding_Family family = Get_Binding_Family(input);
    std::string text;
    while (!binding.empty()) {
        std::string_view part = Take_Binding_Part(binding);
        if (Get_Binding_Family(part) != family) {
            text += text.empty() ? "" : "|";
            text += part;
        }
    }
    text += text.empty() ? "" : "|";
    text += input;
    return text;
}

bool Modifier_Key(SDL_Scancode key) {
    return key == SDL_SCANCODE_LCTRL || key == SDL_SCANCODE_RCTRL || key == SDL_SCANCODE_LSHIFT
        || key == SDL_SCANCODE_RSHIFT || key == SDL_SCANCODE_LALT || key == SDL_SCANCODE_RALT;
}

void Append_Modifiers(std::string& input, SDL_Keymod modifiers, SDL_Scancode exclude = SDL_SCANCODE_UNKNOWN) {
    const SDL_Scancode keys[] = { SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_LALT, SDL_SCANCODE_RALT };
    const SDL_Keymod masks[] = { SDL_KMOD_LCTRL, SDL_KMOD_RCTRL, SDL_KMOD_LSHIFT, SDL_KMOD_RSHIFT, SDL_KMOD_LALT, SDL_KMOD_RALT };
    for (u32 index = 0; index < std::size(keys); index++) {
        if ((modifiers & masks[index]) != 0 && keys[index] != exclude) {
            input += input.empty() ? "" : "&";
            input += "key:";
            input += SDL_GetScancodeName(keys[index]);
        }
    }
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
    else if (setting.key == "rt64.resolutionMultiplier") {
        constexpr f64 step = 0.25;
        s32 index = static_cast<s32>(round(number / step));
        s32 first = static_cast<s32>(round(setting.minimum / step));
        s32 last = static_cast<s32>(round(setting.maximum / step));
        ImGui::SliderInt("##number", &index, first, last, "", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
        number = index * step;
        std::string text = Text_Format("%.3g", number);
        ImGui::RenderTextClipped(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), text.c_str(), nullptr, nullptr, ImVec2(0.5f, 0.5f));
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
    std::string shown = capturing ? "Press keys or buttons (Esc: cancel)" : Describe_Binding(setting.value);
    shown += "###binding";
    f32 button_width = ImGui::CalcTextSize(shown.c_str(), nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    f32 right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(setting.label.c_str());
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - button_width));
    if (ImGui::Button(shown.c_str())) {
        sCaptureGroup = &group;
        sCaptureMouseButtons = 0;
        sCaptureIgnoredMouse = SDL_GetMouseState(nullptr, nullptr);
        sCaptureKey = setting.key;
        sCaptureValue = setting.value;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        sCaptureGroup = capturing ? nullptr : sCaptureGroup;
        sChanges.push_back({ &group, setting.key, "" });
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

void Draw_Setting(Settings_Group& group, const Setting& setting, const char* shortcut = nullptr) {
    ImGui::PushID(&group);
    ImGui::PushID(setting.key.c_str());
    ImGui::BeginDisabled((setting.flags & MODLOADER_SETTING_DISABLED) != 0);
    switch (setting.type) {
    case MODLOADER_SETTING_BOOL:
        if (ImGui::MenuItem(setting.label.c_str(), shortcut, setting.value == "true")) {
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

void Draw_Unthrottled_Limit() {
    for (const std::unique_ptr<Settings_Group>& group : Settings_Groups()) {
        if (group->Name() != "client_settings") {
            continue;
        }
        for (const Setting& setting : group->Entries()) {
            if (setting.key == "emulation.unthrottled_speed_percent") {
                Draw_Setting(*group, setting);
                return;
            }
        }
    }
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

void Draw_Page(const std::string& path, const Toolbar_State& state) {
    std::vector<std::string> children;
    std::string_view child;
    std::string shortcut;

    ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
    for (const std::unique_ptr<Settings_Group>& group : Settings_Groups()) {
        for (const Setting& setting : group->Entries()) {
            if (!setting.Is_Visible() || !Page_Within(setting.page, path)) {
                continue;
            }

            if (setting.page == path) {
                bool speed_limit = (setting.flags & MODLOADER_SETTING_SPEED_LIMIT) != 0;
                Draw_Setting(*group, setting, speed_limit ? Shortcut(state.unthrottledBinding, shortcut) : nullptr);
                if (speed_limit) {
                    Draw_Unthrottled_Limit();
                }
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
            Draw_Page(path + "/" + name, state);
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

bool Toolbar_Draw_Setting(Settings_Group& group, std::string_view key) {
    auto lock = Settings_Lock();
    for (const Setting& setting : group.Entries()) {
        if (setting.key == key) {
            Draw_Setting(group, setting);
            return true;
        }
    }
    return false;
}

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
                Draw_Page("ModLoader", state);
            }

            ImGui::Separator();
            action = ImGui::MenuItem("Quit") ? Toolbar_Action_Quit : action;
            ImGui::EndMenu();
        }

        for (const char* menu : gMenuOrder) {
            bool video = std::string_view(menu) == "Video";
            bool input = std::string_view(menu) == "Input";
            bool settings = Page_Has(menu);

            if ((video || input || settings) && ImGui::BeginMenu(menu)) {
                if (input) {
                    action = ImGui::MenuItem("Lock mouse", Shortcut(state.mouseCaptureBinding, shortcut), state.mouseCaptured) ? Toolbar_Action_Mouse_Capture : action;
                    if (settings) {
                        ImGui::Separator();
                    }
                }
                Draw_Page(menu, state);
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
                Draw_Page(menu, state);
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
    const char* name;

    if (sCaptureGroup == nullptr) {
        return false;
    }
    
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) {
            sCaptureGroup = nullptr;
            return true;
        }
        name = SDL_GetScancodeName(event.key.scancode);
        if (name != nullptr && name[0] != '\0' && ((event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && !Modifier_Key(event.key.scancode)) || (event.type == SDL_EVENT_KEY_UP && Modifier_Key(event.key.scancode) && sCaptureMouseButtons == 0))) {
            Append_Modifiers(input, event.key.mod, event.key.scancode);
            input += input.empty() ? "" : "&";
            input += "key:";
            input += name;
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
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button < 1 || event.button.button > std::size(gMouseNames)) {
            break;
        }
        {
            const u32 button = 1u << (event.button.button - 1);
            if ((sCaptureIgnoredMouse & button) != 0) {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    sCaptureIgnoredMouse &= ~button;
                }
                break;
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (sCaptureMouseButtons == 0) {
                    sCaptureMouseModifiers = SDL_GetModState();
                }
                sCaptureMouseButtons |= button;
            }
            else if ((sCaptureMouseButtons & button) != 0) {
                Append_Modifiers(input, sCaptureMouseModifiers);
                for (u32 index = 0; index < std::size(gMouseNames); index++) {
                    if ((sCaptureMouseButtons & (1u << index)) != 0) {
                        input += input.empty() ? "" : "&";
                        input += "mouse:";
                        input += gMouseNames[index];
                    }
                }
            }
        }
        break;
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        break;
    default:
        return false;
    }
    if (!input.empty()) {
        sChanges.push_back({ sCaptureGroup, sCaptureKey, Merge_Binding(sCaptureValue, input) });
        sCaptureGroup = nullptr;
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

s32 Binding_Mouse_Button(std::string_view name) {
    for (u32 index = 0; index < std::size(gMouseNames); index++) {
        if (name == gMouseNames[index]) {
            return static_cast<s32>(index + 1);
        }
    }
    return -1;
}
