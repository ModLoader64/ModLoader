#include "settings.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <unordered_map>

namespace {

std::mutex sLock;
std::string sConfigDirectory;
std::vector<std::unique_ptr<Settings_Group>> sGroups;
std::unordered_map<std::string, Settings_Group*> sGroupNames;

std::string Format_Float(f64 number) {
    std::string text = Text_Format("%.15g", number);
    return strtod(text.c_str(), nullptr) == number ? text : Text_Format("%.17g", number);
}

std::optional<std::string> Normalize(const Setting& setting, const std::string& value) {
    bool bounded = setting.minimum < setting.maximum;
    char* end;
    f64 number;
    s64 integer;

    switch (setting.type) {
    case MODLOADER_SETTING_BOOL:
        return value == "true" || value == "false" ? std::optional(value) : std::nullopt;
    case MODLOADER_SETTING_INT:
        errno = 0;
        integer = strtoll(value.c_str(), &end, 10);
        number = static_cast<f64>(integer);
        if (errno == ERANGE || end == value.c_str() || *end != '\0' || (bounded && (number < setting.minimum || number > setting.maximum))) {
            return std::nullopt;
        }
        return Text_Format("%lld", static_cast<long long>(integer));
    case MODLOADER_SETTING_FLOAT:
        number = strtod(value.c_str(), &end);
        if (end == value.c_str() || *end != '\0' || !isfinite(number) || (bounded && (number < setting.minimum || number > setting.maximum))) {
            return std::nullopt;
        }
        return Format_Float(number);
    case MODLOADER_SETTING_CHOICE:
        for (const Setting_Choice& choice : Setting_Choices(setting.choices)) {
            if (choice.value == value) {
                return value;
            }
        }
        return std::nullopt;
    default:
        return value;
    }
}

yyjson_mut_val* Lookup(yyjson_mut_doc* document, std::string_view key, bool create, std::string& out_leaf) {
    yyjson_mut_val* object = yyjson_mut_doc_get_root(document);
    yyjson_mut_val* child;
    std::string name;
    usize dot;

    while ((dot = key.find('.')) != std::string_view::npos && object != nullptr) {
        name = key.substr(0, dot);
        child = yyjson_mut_obj_get(object, name.c_str());
        if (!yyjson_mut_is_obj(child)) {
            child = create ? yyjson_mut_obj(document) : nullptr;
            if (child != nullptr) {
                yyjson_mut_obj_put(object, yyjson_mut_strcpy(document, name.c_str()), child);
            }
        }
        object = child;
        key.remove_prefix(dot + 1);
    }

    out_leaf = key;
    return object;
}

std::optional<std::string> Stored_Text(yyjson_mut_doc* document, std::string_view key) {
    std::string leaf;
    yyjson_mut_val* object = Lookup(document, key, false, leaf);
    yyjson_mut_val* value = object != nullptr ? yyjson_mut_obj_get(object, leaf.c_str()) : nullptr;

    if (yyjson_mut_is_bool(value)) {
        return yyjson_mut_get_bool(value) ? "true" : "false";
    }

    if (yyjson_mut_is_int(value)) {
        return Text_Format("%lld", static_cast<long long>(yyjson_mut_get_int(value)));
    }

    if (yyjson_mut_is_real(value)) {
        return Format_Float(yyjson_mut_get_real(value));
    }

    if (yyjson_mut_is_str(value)) {
        return yyjson_mut_get_str(value);
    }

    return std::nullopt;
}

Mutable_Json_Document Load(const std::string& path) {
    yyjson_read_err error = {};
    Json_Document read(yyjson_read_file(path.c_str(), YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS, nullptr, &error));
    if (read != nullptr && yyjson_is_obj(yyjson_doc_get_root(read.get()))) {
        return Mutable_Json_Document(yyjson_doc_mut_copy(read.get(), nullptr));
    }

    if (read != nullptr || error.code != YYJSON_READ_ERROR_FILE_OPEN) {
        Log_Warning(
            "settings",
            "%s: %s at byte %llu; using defaults",
            path.c_str(),
            read == nullptr ? error.msg : "expected JSON object",
            static_cast<unsigned long long>(error.pos)
        );
    }

    Mutable_Json_Document document(yyjson_mut_doc_new(nullptr));
    yyjson_mut_doc_set_root(document.get(), yyjson_mut_obj(document.get()));
    return document;
}

} // namespace

std::vector<Setting_Choice> Setting_Choices(std::string_view choices) {
    std::vector<Setting_Choice> list;
    std::string_view line;
    usize end;
    usize tab;

    while (!choices.empty()) {
        end = choices.find('\n');
        line = choices.substr(0, end);
        tab = line.find('\t');
        list.push_back({ line.substr(0, tab), tab != std::string_view::npos ? line.substr(tab + 1) : line });
        choices.remove_prefix(end != std::string_view::npos ? end + 1 : choices.size());
    }
    return list;
}

Settings_Group::Settings_Group(std::string_view group_name, bool with_file, Settings_Changed on_changed) : name(group_name), changed(std::move(on_changed)) {
    if (with_file) {
        path = Path_Join(sConfigDirectory, name + ".json");
        document = Load(path);
    }
}

void Settings_Group::Store(const Setting& setting) {
    std::string leaf;
    yyjson_mut_doc* json = document.get();
    yyjson_mut_val* object = json != nullptr ? Lookup(json, setting.key, true, leaf) : nullptr;
    yyjson_mut_val* value;

    if (object == nullptr) {
        return;
    }

    switch (setting.type) {
    case MODLOADER_SETTING_BOOL:
        value = yyjson_mut_bool(json, setting.value == "true");
        break;
    case MODLOADER_SETTING_INT:
        value = yyjson_mut_sint(json, strtoll(setting.value.c_str(), nullptr, 10));
        break;
    case MODLOADER_SETTING_FLOAT:
        value = yyjson_mut_real(json, strtod(setting.value.c_str(), nullptr));
        break;
    default:
        value = yyjson_mut_strcpy(json, setting.value.c_str());
        break;
    }

    yyjson_mut_obj_put(object, yyjson_mut_strcpy(json, leaf.c_str()), value);
    if (!Json_Write_File(path, json)) {
        Log_Warning("settings", "cannot write %s", path.c_str());
    }
}

bool Settings_Group::Add(const ModLoader_Setting& description) {
    std::lock_guard lock(sLock);
    Setting* setting;
    std::optional<std::string> stored;
    std::optional<std::string> value;
    bool added = false;

    if (description.key == nullptr || description.key[0] == '\0' || description.type > MODLOADER_SETTING_BINDING) {
        return false;
    }

    setting = Find(description.key);
    if (setting == nullptr) {
        setting = &settings.emplace_back();
        setting->key = description.key;
        added = true;
    }

    setting->label = description.label != nullptr ? description.label : description.key;
    setting->page = description.page != nullptr ? description.page : "";
    setting->help = description.help != nullptr ? description.help : "";
    setting->choices = description.choices != nullptr ? description.choices : "";
    setting->type = description.type;
    setting->flags = description.flags;
    setting->minimum = description.minimum;
    setting->maximum = description.maximum;

    if (!added) {
        return true;
    }

    stored = document != nullptr ? Stored_Text(document.get(), setting->key) : std::nullopt;
    value = stored ? Normalize(*setting, *stored) : std::nullopt;
    if (value) {
        setting->value = *value;
        return true;
    }

    if (stored) {
        Log_Warning("settings", "%s: invalid %s; using default", path.c_str(), setting->key.c_str());
    }

    value = Normalize(*setting, description.value != nullptr ? description.value : "");
    setting->value = value ? *value : std::string(description.value != nullptr ? description.value : "");
    Store(*setting);
    return true;
}

bool Settings_Group::Set(std::string_view key, std::string_view value) {
    std::unique_lock lock(sLock);
    Setting* setting = Find(key);
    std::optional<std::string> normal = setting != nullptr ? Normalize(*setting, std::string(value)) : std::nullopt;
    std::string changed_key;
    std::string changed_value;

    if (!normal) {
        return false;
    }

    if (setting->value == *normal) {
        return true;
    }

    setting->value = *normal;
    Store(*setting);
    changed_key = setting->key;
    changed_value = setting->value;
    lock.unlock();
    if (changed) {
        changed(*this, changed_key, changed_value);
    }

    return true;
}

std::optional<std::string> Settings_Group::Get(std::string_view key) const {
    std::lock_guard lock(sLock);
    const Setting* setting = Find(key);
    return setting != nullptr ? std::optional(setting->value) : std::nullopt;
}

bool Settings_Group::Get_Bool(std::string_view key) const {
    return Get(key) == "true";
}

f64 Settings_Group::Get_Number(std::string_view key) const {
    std::optional<std::string> value = Get(key);
    return value ? strtod(value->c_str(), nullptr) : 0.0;
}

void Settings_Start(const std::string& data_directory) {
    sConfigDirectory = Path_Join(data_directory, "config");
    Directory_Create_All(sConfigDirectory);
}

void Settings_Stop() {
    sGroupNames.clear();
    sGroups.clear();
}

namespace {

Settings_Group* Create_Group(std::string_view name, bool with_file, Settings_Changed changed) {
    auto group = std::make_unique<Settings_Group>(name, with_file, std::move(changed));
    Settings_Group* result = sGroups.emplace_back(std::move(group)).get();
    sGroupNames.emplace(name, result);
    return result;
}

} // namespace

Settings_Group* Settings_Group_Create(std::string_view name, bool with_file, Settings_Changed changed) {
    std::lock_guard lock(sLock);
    return Create_Group(name, with_file, std::move(changed));
}

Settings_Group* Settings_Group_Find_Or_Create(std::string_view name, Settings_Changed changed) {
    std::lock_guard lock(sLock);
    auto found = sGroupNames.find(std::string(name));
    return found != sGroupNames.end() ? found->second : Create_Group(name, true, std::move(changed));
}

std::unique_lock<std::mutex> Settings_Lock() {
    return std::unique_lock(sLock);
}

const std::vector<std::unique_ptr<Settings_Group>>& Settings_Groups() {
    return sGroups;
}
