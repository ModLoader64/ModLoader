// Values persist in <data>/config/<module>.json
#pragma once

#include <modloader/function.h>
#include <modloader/subscription.h>
#include <modloader/types.h>

#include <modloader_settings.h>
#include <optional>
#include <string>
#include <string_view>

namespace ModLoader::Config {

enum class Type : u32 {
    Bool = MODLOADER_SETTING_BOOL,
    Int = MODLOADER_SETTING_INT,
    Float = MODLOADER_SETTING_FLOAT,
    Choice = MODLOADER_SETTING_CHOICE, // one of choices: lines of "value" or "value\tLabel"
    Text = MODLOADER_SETTING_TEXT,
};

struct Setting {
    std::string_view key;
    std::string_view label;
    Type type = Type::Bool;
    std::string_view value;
    std::string_view help;
    std::string_view choices;
    f64 minimum = 0.0;
    f64 maximum = 0.0;
    bool hidden = false; // kept in the file, not shown
};

bool Declare(const Setting& setting);
// nullopt for an unknown key
std::optional<std::string> Get(std::string_view key);
// Numeric getters return zero when the key is missing
f64 Get_Float(std::string_view key);
s64 Get_Int(std::string_view key);
bool Set(std::string_view key, std::string_view value); // Saves and queues a change event
// Runs at the next refresh; repeated changes to the same key are combined; returns 0 on failure
u32 On_Change(Function<void(std::string_view key)> handler);
void Remove_On_Change(u32 handler);
[[nodiscard]] Subscription Subscribe(Function<void(std::string_view)> handler);

static inline bool Get_Bool(std::string_view key) {
    return Get(key) == "true";
}

static inline bool Set_Bool(std::string_view key, bool value) {
    return Set(key, value ? "true" : "false");
}

static inline bool Set_Int(std::string_view key, s64 value) {
    return Set(key, std::to_string(value));
}

static inline bool Declare_Bool(std::string_view key, std::string_view label, bool value) {
    return Declare({.key = key, .label = label, .type = Type::Bool, .value = value ? "true" : "false"});
}

static inline bool Declare_Int(std::string_view key, std::string_view label, s64 value) {
    return Declare({.key = key, .label = label, .type = Type::Int, .value = std::to_string(value)});
}

} // namespace ModLoader::Config


