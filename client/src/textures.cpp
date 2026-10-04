#include "textures.h"
#include "base/json.h"
#include "module.h"

#include <algorithm>

namespace {
bool Valid_Text(std::string_view text) {
    return !text.empty() && text.find('\0') == std::string_view::npos;
}
} // namespace

Texture_Manager::Texture_Manager(Runtime& owner) : runtime(owner) {
    configPath = Path_Join(runtime.config.dataDirectory, "config/textures.json");
    Json_Document document(yyjson_read_file(configPath.c_str(), 0, nullptr, nullptr));
    yyjson_val* array = yyjson_doc_get_root(document.get());
    usize index;
    usize count;
    yyjson_val* item;

    yyjson_arr_foreach(array, index, count, item) {
        const char* folder = yyjson_get_str(yyjson_obj_get(item, "owner"));
        const char* id = yyjson_get_str(yyjson_obj_get(item, "id"));
        if (folder != nullptr && id != nullptr) {
            preferences.push_back({ folder, id, yyjson_get_bool(yyjson_obj_get(item, "enabled")) });
        }
    }

    std::string directory = Path_Join(runtime.config.dataDirectory, "texture_packs");
    Directory_Create_All(directory);
    auto entries = Directory_List(directory);
    std::sort(entries.begin(), entries.end());
    for (const std::string& name : entries) {
        std::string path = Path_Join(directory, name);
        if (Directory_Exists(path) || name.ends_with(".zip") || name.ends_with(".rtz")) {
            Create(nullptr, "", name, name, path, path, false);
        }
    }
}

usize Texture_Manager::Rank(const Source& source) const {
    auto found = std::find_if(preferences.begin(), preferences.end(), [&](const Preference& preference) {
        return preference.owner == source.owner && preference.id == source.id;
    });
    return static_cast<usize>(found - preferences.begin());
}

u32 Texture_Manager::Create(Module* module, std::string owner, std::string id, std::string name, std::string path,
    std::string resolved_path, bool enabled) {
    if (!Valid_Text(id) || !Valid_Text(name) || !Valid_Text(resolved_path)) {
        return 0;
    }

    std::lock_guard guard(lock);
    if (nextHandle == UINT32_MAX || std::any_of(sources.begin(), sources.end(),
    [&](const Source& source) {
        return source.owner == owner && source.id == id;
    })) {
        return 0;
    }

    Source source;
    source.handle = ++nextHandle;
    source.owner = std::move(owner);
    source.id = std::move(id);
    source.name = std::move(name);
    source.path = std::move(path);
    source.resolvedPath = std::move(resolved_path);
    source.module = module;

    usize rank = Rank(source);
    if (rank == preferences.size()) {
        preferences.push_back({ source.owner, source.id, enabled });
    }

    source.enabled = preferences[rank].enabled;
    source.state = source.enabled ? MODLOADER_TEXTURE_PENDING : MODLOADER_TEXTURE_DISABLED;
    sources.insert(std::find_if(sources.begin(), sources.end(), [&](const Source& other) { return Rank(other) > rank; }), std::move(source));
    revision++;
    dirty = savePending = true;
    return nextHandle;
}

bool Texture_Manager::Owns(const Module& module, u32 handle) const {
    std::lock_guard guard(lock);
    const Source* source = Find(handle);
    return source != nullptr && source->module == &module;
}

void Texture_Manager::Destroy(u32 handle) {
    std::lock_guard guard(lock);
    if (std::erase_if(sources, [=](const Source& source) { return source.handle == handle; }) != 0) {
        revision++;
        dirty = true;
    }
}

void Texture_Manager::Release(Module& module) {
    std::lock_guard guard(lock);
    if (std::erase_if(sources, [&](const Source& source) { return source.module == &module; }) != 0) {
        revision++;
        dirty = true;
    }
}

std::vector<Texture_Source_View> Texture_Manager::Snapshot() const {
    std::lock_guard guard(lock);
    return { sources.begin(), sources.end() };
}

bool Texture_Manager::Enable(u32 handle, bool enabled) {
    std::lock_guard guard(lock);
    Source* source = Find(handle);
    if (source == nullptr) {
        return false;
    }

    if (source->enabled != enabled) {
        source->enabled = enabled;
        source->state = enabled ? MODLOADER_TEXTURE_PENDING : MODLOADER_TEXTURE_DISABLED;
        preferences[Rank(*source)].enabled = enabled;
        revision++;
        dirty = savePending = true;
    }

    return true;
}

bool Texture_Manager::Move(u32 handle, s32 delta) {
    std::lock_guard guard(lock);
    Source* source = Find(handle);
    if (source == nullptr) {
        return false;
    }

    s64 index = source - sources.data();
    s64 destination = index + delta;
    if (destination < 0 || destination >= static_cast<s64>(sources.size())) {
        return false;
    }

    std::swap(preferences[Rank(*source)], preferences[Rank(sources[destination])]);
    std::swap(sources[index], sources[destination]);
    revision++;
    dirty = savePending = true;
    return true;
}

bool Texture_Manager::Reload(u32 handle) {
    std::lock_guard guard(lock);
    if (Find(handle) == nullptr) {
        return false;
    }

    revision++;
    dirty = true;
    return true;
}

u32 Texture_Manager::State(u32 handle) const {
    std::lock_guard guard(lock);
    const Source* source = Find(handle);
    return source != nullptr ? source->state : MODLOADER_TEXTURE_DISABLED;
}

bool Texture_Manager::Is_Enabled(u32 handle) const {
    std::lock_guard guard(lock);
    const Source* source = Find(handle);
    return source != nullptr && source->enabled;
}

void Texture_Manager::Save() {
    Mutable_Json_Document document(yyjson_mut_doc_new(nullptr));
    yyjson_mut_val* array = yyjson_mut_arr(document.get());
    yyjson_mut_doc_set_root(document.get(), array);
    for (const Preference& preference : preferences) {
        yyjson_mut_val* item = yyjson_mut_obj(document.get());
        yyjson_mut_obj_add_strcpy(document.get(), item, "owner", preference.owner.c_str());
        yyjson_mut_obj_add_strcpy(document.get(), item, "id", preference.id.c_str());
        yyjson_mut_obj_add_bool(document.get(), item, "enabled", preference.enabled);
        yyjson_mut_arr_append(array, item);
    }

    if (!Json_Write_File(configPath, document.get())) {
        Log_Warning("textures", "cannot save %s", configPath.c_str());
    }
}

void Texture_Manager::Dispatch() {
    if (dispatching) {
        return;
    }

    dispatching = true;
    std::vector<std::string> paths;
    u64 applied_revision;
    bool reload;
    {
        std::lock_guard guard(lock);
        if (std::erase_if(sources, [](const Source& source) { return source.module != nullptr && source.module->disabled; }) != 0) {
            revision++;
            dirty = true;
        }

        for (auto source = sources.rbegin(); source != sources.rend(); source++) {
            if (source->enabled) {
                paths.push_back(source->resolvedPath);
            }
        }

        applied_revision = revision;
        reload = dirty;
        dirty = false;
        if (savePending) {
            Save();
            savePending = false;
        }
    }

    std::vector<const char*> pointers;
    for (const std::string& path : paths) {
        pointers.push_back(path.c_str());
    }

    s32 state = runtime.Host().Set_Texture_Sources(pointers, reload ? MODLOADER_TEXTURE_RELOAD : 0);
    if (state < MODLOADER_TEXTURE_PENDING || state > MODLOADER_TEXTURE_ERROR) {
        state = MODLOADER_TEXTURE_ERROR;
    }

    std::vector<std::pair<Module*, ModLoader_Texture_Event>> events;
    {
        std::lock_guard guard(lock);
        if (revision == applied_revision) {
            for (usize index = 0; index < sources.size(); index++) {
                Source& source = sources[index];
                source.state = source.enabled ? static_cast<u32>(state) : MODLOADER_TEXTURE_DISABLED;
                if (source.notifiedState != source.state || source.notifiedPriority != index) {
                    if (source.module != nullptr) {
                        events.push_back({ source.module, { { runtime.eventTime, Time_Monotonic_Milliseconds() }, source.handle,
                            source.state, source.enabled ? 1u : 0u, static_cast<u32>(index) } });
                    }
                }
            }
        }
    }

    for (const auto& [module, event] : events) {
        {
            std::lock_guard guard(lock);
            if (revision != applied_revision) {
                break;
            }

            if (module->stopping || module->disabled) {
                continue;
            }
            
            Source* source = Find(event.source);
            source->notifiedState = event.state;
            source->notifiedPriority = event.priority;
        }
        module->Deliver(MODLOADER_EVENT_TEXTURE_SOURCE, &event, sizeof(event));
    }
    dispatching = false;
}
