#pragma once

#include "base.h"
#include "wasm_export.h"

#include <memory>
#include <unordered_map>

struct Module;

inline constexpr std::string_view gLayoutPrefix = "modloader.layout.";
inline constexpr u32 gLayoutVersion = 1;

struct Guest_Layout_Field {
    std::string name;
    std::string record;
    std::string kind;
    u64 offset = 0;
    u64 size = 0;
    u64 alignment = 1;
    u64 count = 1;
    bool absent = false;

    bool operator==(const Guest_Layout_Field&) const = default;
};

struct Guest_Record_Layout {
    std::string name;
    u64 size = 0;
    u64 alignment = 1;
    bool isUnion = false;
    std::vector<Guest_Layout_Field> fields;

    bool operator==(const Guest_Record_Layout&) const = default;
};

struct Guest_Layout_Edit {
    enum class Kind { Record, Field, Insert, Remove };

    Kind kind = Kind::Record;
    std::string record;
    std::string field;
    std::optional<u64> offset;
    std::optional<u64> size;
    std::optional<u64> alignment;
    bool shiftFollowing = false;
    bool absent = false;
};

struct Guest_Layout_Profile {
    const Module* owner = nullptr;
    std::vector<Guest_Layout_Edit> edits;
};

using Guest_Layout_Values = std::unordered_map<std::string, u64>;
using Guest_Layout_Snapshot = std::shared_ptr<const Guest_Layout_Values>;

class Guest_Layouts {
public:
    bool Register(std::string_view description, std::string& error);
    bool Refresh(std::string& error);
    u32 Load(const Module& owner, std::string_view json, std::string& error);
    Guest_Layout_Snapshot Select(const Module& owner, u32 handle, std::string& error);
    void Unload(const Module& owner, u32 handle);
    void Release(const Module& owner);
    Guest_Layout_Snapshot Snapshot();

private:
    Guest_Layout_Snapshot Resolve(const Guest_Layout_Profile& profile, std::string& error) const;
    std::mutex lock;
    std::unordered_map<std::string, Guest_Record_Layout> records;
    std::unordered_map<u32, Guest_Layout_Profile> profiles;
    Guest_Layout_Snapshot active = std::make_shared<Guest_Layout_Values>();
    Guest_Layout_Profile activeProfile;
    u32 nextHandle = 0;
};

struct Module_Layout_Slot {
    std::string name;
    u64 baseline;
};

struct Module_Layout_Binding {
    Guest_Layout_Snapshot snapshot;
    std::vector<void*> addresses;
};

bool Layouts_Bind(Module& module);
bool Layouts_Apply(Module& module, wasm_module_inst_t instance, const Guest_Layout_Snapshot& snapshot);
void Layouts_Register_Natives();

class Layout_Execution {
public:
    Layout_Execution(Module& module, wasm_module_inst_t instance);
    ~Layout_Execution();
    bool Ready() const {
        return ready;
    }
    static bool Select(Module& module, wasm_module_inst_t instance, u32 handle);

private:
    Module& module;
    wasm_module_inst_t instance;
    Guest_Layout_Snapshot snapshot;
    Layout_Execution* previous;
    bool ready;
};
