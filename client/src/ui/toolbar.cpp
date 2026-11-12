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
constexpr const char* gCategories[] = { "Video", "Input", "Emulation" };
constexpr const char* gTexturesPage = "Texture replacements";
constexpr const char* gFaceButtonNames[] = { "south", "east", "west", "north" };

struct Change {
    Settings_Group* group;
    std::string key;
    std::string value;
};

std::vector<Change> sChanges;
std::vector<SDL_GamepadBinding> sGamepadBindings;
bool sXboxGamepad;

Settings_Group* sCaptureGroup;
std::string sCaptureKey;
std::string sCaptureValue;
u32 sCaptureMouseButtons;
u32 sCaptureIgnoredMouse;
SDL_Keymod sCaptureMouseModifiers;
constexpr const char* gMouseNames[] = { "left", "middle", "right", "side1", "side2" };

bool sMenuHidden;
bool sSettingsOpen;
std::string sSettingsPage;

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

std::string Describe_Gamepad_Binding(std::string_view part) {
    const bool axis = part.starts_with("axis:");
    std::string name(part.substr(axis ? 5 : 4));
    const bool positive = !axis || name.empty() || name.back() != '-';
    if (axis && !name.empty() && (name.back() == '+' || name.back() == '-')) {
        name.pop_back();
    }
    const s32 code = axis ? SDL_GetGamepadAxisFromString(name.c_str()) : Binding_Pad_Button(name);
    if (sXboxGamepad) {
        constexpr const char* face_names[] = { "A", "B", "X", "Y" };
        if (!axis && code >= 0 && code < static_cast<s32>(std::size(face_names))) {
            return std::string("Pad ") + face_names[code];
        }
        return std::string("Pad ") + name + (axis ? (positive ? "+" : "-") : "");
    }

    std::string text;
    for (const SDL_GamepadBinding& binding : sGamepadBindings) {
        bool maximum = true;
        if (axis) {
            if (binding.output_type != SDL_GAMEPAD_BINDTYPE_AXIS || binding.output.axis.axis != code) {
                continue;
            }
            const s32 minimum = binding.output.axis.axis_min;
            const s32 maximum_value = binding.output.axis.axis_max;
            const s32 endpoint = positive ? std::max(minimum, maximum_value) : std::min(minimum, maximum_value);
            if ((positive && endpoint <= 0) || (!positive && endpoint >= 0)) {
                continue;
            }
            maximum = endpoint == maximum_value;
        }
        else if (binding.output_type != SDL_GAMEPAD_BINDTYPE_BUTTON || binding.output.button != code) {
            continue;
        }

        std::string physical;
        switch (binding.input_type) {
        case SDL_GAMEPAD_BINDTYPE_BUTTON:
            physical = Text_Format("Button %d", binding.input.button + 1);
            break;
        case SDL_GAMEPAD_BINDTYPE_AXIS:
            {
                const s32 endpoint = maximum ? binding.input.axis.axis_max : binding.input.axis.axis_min;
                const s32 other_endpoint = maximum ? binding.input.axis.axis_min : binding.input.axis.axis_max;
                physical = Text_Format("Axis %d %c", binding.input.axis.axis + 1, endpoint > other_endpoint ? '+' : '-');
            }
            break;
        case SDL_GAMEPAD_BINDTYPE_HAT:
            physical = Text_Format("Hat %d", binding.input.hat.hat + 1);
            if (binding.input.hat.hat_mask & SDL_HAT_UP) {
                physical += " up";
            }
            if (binding.input.hat.hat_mask & SDL_HAT_DOWN) {
                physical += " down";
            }
            if (binding.input.hat.hat_mask & SDL_HAT_LEFT) {
                physical += " left";
            }
            if (binding.input.hat.hat_mask & SDL_HAT_RIGHT) {
                physical += " right";
            }
            break;
        default:
            continue;
        }
        text += text.empty() ? "" : " / ";
        text += physical;
    }
    if (!text.empty()) {
        return "Pad " + text;
    }
    if (code < 0) {
        return std::string("Mapped ") + (axis ? "axis " : "button ") + name;
    }
    return axis ? Text_Format("Mapped axis %d %c", code + 1, positive ? '+' : '-')
                : Text_Format("Mapped button %d", code + 1);
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
                text += Describe_Gamepad_Binding(part);
                continue;
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

void Draw_Choice(Settings_Group& group, const Setting& setting, bool window) {
    std::vector<Setting_Choice> choices = Setting_Choices(setting.choices);
    std::string_view shown = setting.value;
    std::string label;

    for (const Setting_Choice& choice : choices) {
        shown = choice.value == setting.value ? choice.label : shown;
    }

    if (window) {
        label = shown;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
        if (ImGui::BeginCombo("##choice", label.c_str())) {
            for (const Setting_Choice& choice : choices) {
                label = choice.label;
                if (ImGui::Selectable(label.c_str(), choice.value == setting.value)) {
                    sChanges.push_back({ &group, setting.key, std::string(choice.value) });
                }
            }
            ImGui::EndCombo();
        }
        Draw_Tooltip(setting);
        ImGui::SameLine();
        ImGui::TextUnformatted(setting.label.c_str());
        return;
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

// window: widgets for a window, else for a menu
void Draw_Setting(Settings_Group& group, const Setting& setting, const char* shortcut = nullptr, bool window = false) {
    bool enabled = setting.value == "true";

    ImGui::PushID(&group);
    ImGui::PushID(setting.key.c_str());
    ImGui::BeginDisabled((setting.flags & MODLOADER_SETTING_DISABLED) != 0);
    switch (setting.type) {
    case MODLOADER_SETTING_BOOL:
        if (window ? ImGui::Checkbox(setting.label.c_str(), &enabled) : ImGui::MenuItem(setting.label.c_str(), shortcut, enabled)) {
            sChanges.push_back({ &group, setting.key, setting.value == "true" ? "false" : "true" });
        }
        Draw_Tooltip(setting);
        if (window && shortcut != nullptr) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", shortcut);
        }
        break;
    case MODLOADER_SETTING_INT:
    case MODLOADER_SETTING_FLOAT:
        Draw_Number(group, setting);
        break;
    case MODLOADER_SETTING_CHOICE:
        Draw_Choice(group, setting, window);
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

void Draw_Unthrottled_Limit(bool window) {
    for (const std::unique_ptr<Settings_Group>& group : Settings_Groups()) {
        if (group->Name() != "client_settings") {
            continue;
        }
        for (const Setting& setting : group->Entries()) {
            if (setting.key == "emulation.unthrottled_speed_percent") {
                Draw_Setting(*group, setting, nullptr, window);
                return;
            }
        }
    }
}

// The speed limit shows its hotkey, and the unthrottled limit after it
void Draw_Page_Setting(Settings_Group& group, const Setting& setting, const Toolbar_State& state, bool window) {
    bool speed_limit = (setting.flags & MODLOADER_SETTING_SPEED_LIMIT) != 0;
    std::string shortcut;

    Draw_Setting(group, setting, speed_limit ? Shortcut(state.unthrottledBinding, shortcut) : nullptr, window);
    if (speed_limit) {
        Draw_Unthrottled_Limit(window);
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

std::vector<std::string> Child_Pages(const std::string& path) {
    std::vector<std::string> children;
    std::string_view child;

    for (const std::unique_ptr<Settings_Group>& group : Settings_Groups()) {
        for (const Setting& setting : group->Entries()) {
            if (!setting.Is_Visible() || !Page_Within(setting.page, path) || setting.page == path) {
                continue;
            }

            child = std::string_view(setting.page).substr(path.size() + 1);
            child = child.substr(0, child.find('/'));
            if (std::find(children.begin(), children.end(), child) == children.end()) {
                children.emplace_back(child);
            }
        }
    }
    return children;
}

bool Page_Unchosen(std::string_view path) {
    usize separator = path.rfind('/');
    if (separator == std::string_view::npos) {
        return false;
    }

    for (const std::unique_ptr<Settings_Group>& group : Settings_Groups()) {
        for (const Setting& setting : group->Entries()) {
            if (setting.type != MODLOADER_SETTING_CHOICE || setting.page != path.substr(0, separator)) {
                continue;
            }
            for (const Setting_Choice& choice : Setting_Choices(setting.choices)) {
                if (choice.label == path.substr(separator + 1)) {
                    return choice.value != setting.value;
                }
            }
        }
    }
    return false;
}

void Draw_Page(const std::string& path, const Toolbar_State& state, bool window = false) {
    ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
    for (const std::unique_ptr<Settings_Group>& group : Settings_Groups()) {
        for (const Setting& setting : group->Entries()) {
            if (setting.Is_Visible() && setting.page == path) {
                Draw_Page_Setting(*group, setting, state, window);
            }
        }
    }

    ImGui::PopItemFlag();
    if (window) {
        return;
    }

    for (const std::string& name : Child_Pages(path)) {
        if (ImGui::BeginMenu(name.c_str())) {
            Draw_Page(path + "/" + name, state);
            ImGui::EndMenu();
        }
    }
}

void Draw_Speed_Limit(const Toolbar_State& state) {
    ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
    for (const std::unique_ptr<Settings_Group>& group : Settings_Groups()) {
        for (const Setting& setting : group->Entries()) {
            if ((setting.flags & MODLOADER_SETTING_SPEED_LIMIT) != 0 && setting.Is_Visible()) {
                Draw_Page_Setting(*group, setting, state, false);
            }
        }
    }
    ImGui::PopItemFlag();
}

void Open_Settings(const char* page) {
    sSettingsOpen = true;
    sSettingsPage = page;
    ImGui::SetWindowFocus("###emulator_settings");
}

void Draw_Category(const std::string& path, const char* name) {
    std::vector<std::string> children = Child_Pages(path);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;

    flags |= children.empty() ? ImGuiTreeNodeFlags_Leaf : 0;
    flags |= sSettingsPage == path ? ImGuiTreeNodeFlags_Selected : 0;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(Page_Unchosen(path) ? ImGuiCol_TextDisabled : ImGuiCol_Text));
    bool open = ImGui::TreeNodeEx(name, flags);
    ImGui::PopStyleColor();
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        sSettingsPage = path;
    }

    if (open) {
        for (const std::string& child : children) {
            Draw_Category(path + "/" + child, child.c_str());
        }
        ImGui::TreePop();
    }
}

// Categories on the left, selected settings on the right
void Draw_Settings(const Toolbar_State& state, std::unique_lock<std::mutex>& lock) {
    if (!sSettingsOpen) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 46.0f, ImGui::GetFontSize() * 26.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin((state.emulator + " settings###emulator_settings").c_str(), &sSettingsOpen)) {
        ImGui::BeginChild("categories", ImVec2(ImGui::GetFontSize() * 15.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        for (const char* category : gCategories) {
            if (Page_Has(category)) {
                Draw_Category(category, category);
            }
        }
        Draw_Category(gTexturesPage, gTexturesPage);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("page");
        if (sSettingsPage == gTexturesPage) {
            lock.unlock();
            Textures_Draw(state.textures);
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(Page_Unchosen(sSettingsPage) ? ImGuiCol_TextDisabled : ImGuiCol_Text));
            Draw_Page(sSettingsPage, state, true);
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

std::vector<std::string> Other_Menus() {
    std::vector<std::string> menus;
    std::string menu;

    for (const std::unique_ptr<Settings_Group>& group : Settings_Groups()) {
        for (const Setting& setting : group->Entries()) {
            menu = setting.page.substr(0, setting.page.find('/'));
            if (!menu.empty() && menu != "ModLoader" && menu != "Plugins" && std::find(std::begin(gCategories), std::end(gCategories), menu) == std::end(gCategories) &&
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

    {
        std::unique_lock lock = Settings_Lock();

        if (!sMenuHidden && ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("ModLoader")) {
                action = ImGui::MenuItem(state.paused ? "Resume" : "Pause", Shortcut(state.pauseBinding, shortcut)) ? Toolbar_Action_Pause : action;
                action = ImGui::MenuItem("Restart", Shortcut(state.restartBinding, shortcut)) ? Toolbar_Action_Restart : action;
                if (state.savestates) {
                    ImGui::Separator();
                    action = ImGui::MenuItem("Save state", "F5") ? Toolbar_Action_Save_State : action;
                    action = ImGui::MenuItem("Load state", "F7") ? Toolbar_Action_Load_State : action;
                }

                ImGui::Separator();
                action = ImGui::MenuItem("Lock mouse", Shortcut(state.mouseCaptureBinding, shortcut), state.mouseCaptured) ? Toolbar_Action_Mouse_Capture : action;
                Draw_Page("ModLoader", state);
                if (ImGui::MenuItem("Hide menu bar", "Shift+F12")) {
                    sMenuHidden = true;
                }

                ImGui::Separator();
                action = ImGui::MenuItem("Quit") ? Toolbar_Action_Quit : action;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(state.emulator.c_str())) {
                for (const char* category : gCategories) {
                    if (Page_Has(category) && ImGui::MenuItem(category)) {
                        Open_Settings(category);
                    }
                }
                if (ImGui::MenuItem(gTexturesPage)) {
                    Open_Settings(gTexturesPage);
                }

                ImGui::Separator();
                Draw_Speed_Limit(state);
                ImGui::EndMenu();
            }

            if (Page_Has("Plugins") && ImGui::BeginMenu("Plugins")) {
                Draw_Page("Plugins", state);
                ImGui::EndMenu();
            }

            for (const std::string& menu : Other_Menus()) {
                if (ImGui::BeginMenu(menu.c_str())) {
                    Draw_Page(menu, state);
                    ImGui::EndMenu();
                }
            }
            ImGui::EndMainMenuBar();
        }

        Draw_Settings(state, lock);
    }

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
        name = event.gbutton.button < std::size(gFaceButtonNames)
            ? gFaceButtonNames[event.gbutton.button]
            : SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(event.gbutton.button));
        if (name != nullptr) {
            input = std::string("pad:") + name;
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

void Toolbar_Hide_Menu(bool hidden) {
    sMenuHidden = hidden;
}

bool Toolbar_Menu_Hidden() {
    return sMenuHidden;
}

s32 Binding_Pad_Button(std::string_view name) {
    for (u32 index = 0; index < std::size(gFaceButtonNames); index++) {
        if (name == gFaceButtonNames[index]) {
            return static_cast<s32>(index);
        }
    }
    return SDL_GetGamepadButtonFromString(std::string(name).c_str());
}

s32 Binding_Mouse_Button(std::string_view name) {
    for (u32 index = 0; index < std::size(gMouseNames); index++) {
        if (name == gMouseNames[index]) {
            return static_cast<s32>(index + 1);
        }
    }
    return -1;
}

void Toolbar_Set_Gamepad(SDL_Gamepad* gamepad) {
    sGamepadBindings.clear();
    sXboxGamepad = false;
    if (gamepad == nullptr) {
        return;
    }
    const SDL_GamepadType type = SDL_GetGamepadType(gamepad);
    sXboxGamepad = type == SDL_GAMEPAD_TYPE_XBOX360 || type == SDL_GAMEPAD_TYPE_XBOXONE;
    s32 count = 0;
    SDL_GamepadBinding** bindings = SDL_GetGamepadBindings(gamepad, &count);
    if (bindings != nullptr) {
        for (s32 index = 0; index < count; index++) {
            sGamepadBindings.push_back(*bindings[index]);
        }
        SDL_free(bindings);
    }
}
