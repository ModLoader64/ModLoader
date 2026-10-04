#pragma once

#include "base.h"
#include "modloader_texture.h"

#include <algorithm>

struct Module;
class Runtime;

struct Texture_Source_View {
    u32 handle;
    std::string owner;
    std::string id;
    std::string name;
    std::string path;
    bool enabled;
    u32 state;
};

class Texture_Manager {
public:
    explicit Texture_Manager(Runtime& runtime);
    u32 Create(Module* module, std::string owner, std::string id, std::string name, std::string path, std::string resolved_path, bool enabled);
    bool Owns(const Module& module, u32 handle) const;
    void Destroy(u32 handle);
    void Release(Module& module);
    std::vector<Texture_Source_View> Snapshot() const;
    bool Enable(u32 handle, bool enabled);
    bool Move(u32 handle, s32 delta);
    bool Reload(u32 handle);
    u32 State(u32 handle) const;
    bool Is_Enabled(u32 handle) const;
    void Dispatch();

private:
    struct Source : Texture_Source_View {
        Module* module;
        std::string resolvedPath;
        u32 notifiedState = UINT32_MAX;
        u32 notifiedPriority = UINT32_MAX;
    };

    struct Preference {
        std::string owner;
        std::string id;
        bool enabled;
    };

    auto* Find(this auto& manager, u32 handle) {
        auto found = std::ranges::find(manager.sources, handle, &Source::handle);
        return found != manager.sources.end() ? &*found : nullptr;
    }
    
    usize Rank(const Source& source) const;
    void Save();

    Runtime& runtime;
    mutable std::mutex lock;
    std::vector<Source> sources;
    std::vector<Preference> preferences;
    std::string configPath;
    u32 nextHandle = 0;
    u64 revision = 0;
    bool dirty = false;
    bool savePending = false;
    bool dispatching = false;
};

void Textures_Register_Natives();
