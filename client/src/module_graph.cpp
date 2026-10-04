#include "module_graph.h"
#include "module_package.h"
#include "sha1.h"
#include "wasm_object.h"
#include "base/json.h"

#include <modloader/detail/metadata.h>
#include <algorithm>

namespace {

enum class Visit_State {
    Unseen,
    Visiting,
    Complete,
};

std::string_view Json_Text(yyjson_val* value) {
    return yyjson_is_str(value) ? std::string_view(yyjson_get_str(value), yyjson_get_len(value)) : std::string_view();
}

std::optional<std::string> Relative_File(const std::string& source, std::string_view relative) {
    if (!Module_Relative_Path_Is_Valid(relative)) {
        Log_Error("linker", "invalid relative path in %s", source.c_str());
        return std::nullopt;
    }
    std::string directory = Path_Directory(source);
    auto path = Path_Canonical(Path_Join(directory, relative));
    if (path && !path->starts_with(directory + "/")) {
        Log_Error("linker", "path escapes module directory: %s", source.c_str());
        return std::nullopt;
    }
    return path;
}

Json_Document Read_Metadata(std::span<const u8> bytes, const std::string& path) {
    Json_Document document(yyjson_read(reinterpret_cast<const char*>(bytes.data()), bytes.size(), 0));
    if (!document || !yyjson_is_obj(yyjson_doc_get_root(document.get())) ||
        yyjson_get_uint(yyjson_obj_get(yyjson_doc_get_root(document.get()), "version")) != MODLOADER_METADATA_VERSION) {
        Log_Error("linker", "invalid module metadata: %s", path.c_str());
        document.reset();
    }
    return document;
}

class Module_Set {
public:
    explicit Module_Set(Runtime& runtime) : runtime(runtime) {
    }

    std::optional<usize> Read(const std::string& path) {
        auto canonical = Path_Canonical(path);
        if (!canonical) {
            return std::nullopt;
        }

        auto previous_path = paths.find(*canonical);
        if (previous_path != paths.end()) {
            return previous_path->second;
        }

        if (!runtime.modules.empty()) {
            return std::nullopt;
        }
        
        std::optional<Module_Package> package;
        if (std::string_view(*canonical).ends_with(".pak")) {
            package = Open_Module_Package(*canonical, runtime.config.dataDirectory);
            if (!package) {
                return std::nullopt;
            }
        }

        auto bytes = File_Read(package ? package->modulePath : *canonical);
        if (!bytes) {
            Log_Error("linker", "cannot read module %s", path.c_str());
            return std::nullopt;
        }

        auto object = std::move(*bytes);
        Wasm_Metadata metadata;
        if (!Wasm_Read_Metadata(object, metadata) || metadata.module.empty() || !metadata.relocatable) {
            Log_Error("linker", "not a linkable ModLoader module: %s", path.c_str());
            return std::nullopt;
        }

        auto description = Read_Module_Metadata(metadata.module, *canonical);
        if (!description) {
            return std::nullopt;
        }

        Module_Link_Input module;
        module.manifest = std::move(description->manifest);
        if (package) {
            module.packageFingerprint = package->fingerprint;
            auto manifest = Parse_Module_Manifest(package->manifest, *canonical);
            if (!manifest) {
                return std::nullopt;
            }

            if (!Module_Manifest_Matches(*manifest, module.manifest)) {
                Log_Error("linker", "manifest mismatch: %s", path.c_str());
                return std::nullopt;
            }

            module.manifest.assetRoot = Path_Join(package->root, "assets");
            for (const std::string* asset : { &module.manifest.icon, &module.manifest.banner }) {
                if (!asset->empty() && !File_Exists(Path_Join(module.manifest.assetRoot, *asset))) {
                    Log_Error("linker", "missing manifest asset %s in %s", asset->c_str(), path.c_str());
                    return std::nullopt;
                }
            }
        }
        else {
            module.manifest.assetRoot = Path_Join(Path_Directory(*canonical), module.manifest.name + ".assets");
        }

        auto runtime_path = Relative_File(*canonical, description->runtime);
        auto runtime_index = runtime_path ? Read_Runtime(*runtime_path, description->runtimeHash) : std::nullopt;
        if (!runtime_index) {
            return std::nullopt;
        }

        module.runtimeIndex = *runtime_index;
        module.object = std::move(object);
        Sha1 hash;
        hash.Update_Field(module.manifest.name);
        hash.Update_Field(runtimes[module.runtimeIndex].fingerprint);
        for (const std::string& dependency : module.manifest.dependencies) {
            hash.Update_Field(dependency);
        }

        hash.Update_Field(Sha1::Digest(module.object));
        module.fingerprint = hash.Hex_Digest();
        auto previous_name = names.find(module.manifest.name);
        if (previous_name != names.end()) {
            usize index = previous_name->second;
            if (modules[index].fingerprint != module.fingerprint || modules[index].packageFingerprint != module.packageFingerprint) {
                Log_Error("linker", "incompatible copies of module %s", module.manifest.name.c_str());
                return std::nullopt;
            }
            paths.emplace(*canonical, index);
            return index;
        }

        auto previous_folder = folders.find(module.manifest.folder);
        if (previous_folder != folders.end()) {
            Log_Error("linker", "%s and %s share folder %s",
                modules[previous_folder->second].manifest.name.c_str(), module.manifest.name.c_str(), module.manifest.folder.c_str());
            return std::nullopt;
        }

        usize index = modules.size();
        names.emplace(module.manifest.name, index);
        folders.emplace(module.manifest.folder, index);
        paths.emplace(*canonical, index);
        modules.push_back(std::move(module));
        visits.push_back(Visit_State::Unseen);
        groupParents.push_back(index);
        return index;
    }

    bool Visit(usize index) {
        if (visits[index] == Visit_State::Complete) {
            return true;
        }

        if (visits[index] == Visit_State::Visiting) {
            Log_Error("linker", "dependency cycle at %s", modules[index].manifest.name.c_str());
            return false;
        }

        visits[index] = Visit_State::Visiting;
        std::string directory = Path_Directory(modules[index].manifest.sourcePath);
        std::vector<std::string> dependencies = modules[index].manifest.dependencies;
        for (const std::string& dependency : dependencies) {
            auto found = names.find(dependency);
            std::optional<usize> target;
            std::string sibling = Path_Join(directory, dependency + ".pak");
            if (!File_Exists(sibling)) {
                sibling = Path_Join(directory, dependency + ".wasm");
            }

            if (File_Exists(sibling)) {
                target = Read(sibling);
            }
            else if (found != names.end()) {
                target = found->second;
            }
            else {
                Log_Error("linker", "%s requires %s", modules[index].manifest.name.c_str(), dependency.c_str());
                return false;
            }

            if (!target) {
                return false;
            }
            if (modules[*target].manifest.name != dependency) {
                Log_Error("linker", "expected %s, found %s", dependency.c_str(), modules[*target].manifest.name.c_str());
                return false;
            }
            if (!Visit(*target)) {
                return false;
            }

            if (modules[index].runtimeIndex != modules[*target].runtimeIndex) {
                Log_Error("linker", "SDK mismatch: %s and %s", modules[index].manifest.name.c_str(), dependency.c_str());
                return false;
            }
            groupParents[Group(index)] = Group(*target);
        }

        visits[index] = Visit_State::Complete;
        order.push_back(index);
        return true;
    }

    std::vector<std::vector<usize>> Groups() {
        std::vector<std::vector<usize>> groups(modules.size());
        for (usize index : order) {
            groups[Group(index)].push_back(index);
        }

        std::erase_if(groups, [](const auto& group) {
            return group.empty();
        });
        return groups;
    }

    Runtime& runtime;
    std::vector<Module_Link_Runtime> runtimes;
    std::vector<Module_Link_Input> modules;
    std::vector<usize> order;

private:
    usize Group(usize index) {
        while (groupParents[index] != index) {
            groupParents[index] = groupParents[groupParents[index]];
            index = groupParents[index];
        }
        return index;
    }

    std::optional<usize> Read_Runtime(const std::string& path, std::string_view expected_hash) {
        auto previous = runtimePaths.find(path);
        if (previous != runtimePaths.end()) {
            return Check_Runtime_Hash(path, expected_hash, runtimes[previous->second].fingerprint) ? std::optional(previous->second) : std::nullopt;
        }

        auto bytes = File_Read(path);
        Wasm_Metadata metadata;
        if (!bytes || !Wasm_Read_Metadata(*bytes, metadata) || metadata.runtime.empty() || !metadata.relocatable) {
            Log_Error("linker", "not a ModLoader runtime: %s", path.c_str());
            return std::nullopt;
        }

        Module_Link_Runtime loaded;
        loaded.fingerprint = Sha1::Digest(*bytes);
        if (!Check_Runtime_Hash(path, expected_hash, loaded.fingerprint)) {
            return std::nullopt;
        }

        auto matching = runtimeHashes.find(loaded.fingerprint);
        if (matching != runtimeHashes.end()) {
            runtimePaths.emplace(path, matching->second);
            return matching->second;
        }

        auto document = Read_Metadata(metadata.runtime, path);
        if (!document) {
            return std::nullopt;
        }

        yyjson_val* root = yyjson_doc_get_root(document.get());
        std::string_view platform = Json_Text(yyjson_obj_get(root, "platform"));
        std::string_view target = Json_Text(yyjson_obj_get(root, "target"));
        yyjson_val* stack = yyjson_obj_get(root, "stackSize");
        yyjson_val* memory = yyjson_obj_get(root, "maxMemory");
        loaded.stackSize = yyjson_get_uint(stack);
        loaded.maxMemory = yyjson_get_uint(memory);
        bool platform_mismatch = (runtime.config.session & MODLOADER_SESSION_GAME) != 0 && platform != "generic" && platform != runtime.platform.identifier;
        if (platform_mismatch || target != "wasm64" || !yyjson_is_uint(stack) || !yyjson_is_uint(memory) ||
            loaded.stackSize < 16 || loaded.stackSize % 16 != 0 || loaded.maxMemory <= loaded.stackSize ||
            loaded.maxMemory % 65536 != 0 || loaded.maxMemory > 281474976645120ULL) {
            Log_Error("linker", "incompatible runtime %s", path.c_str());
            return std::nullopt;
        }

        if ((runtime.config.session & MODLOADER_SESSION_GAME) == 0) {
            runtime.Register_Platform_Natives(platform);
        }

        usize index = runtimes.size();
        runtimePaths.emplace(path, index);
        runtimeHashes.emplace(loaded.fingerprint, index);
        loaded.object = std::move(*bytes);
        runtimes.push_back(std::move(loaded));
        return index;
    }

    bool Check_Runtime_Hash(const std::string& path, std::string_view expected, std::string_view actual) {
        if (expected != actual) {
            Log_Error("linker", "SDK mismatch: %s", path.c_str());
            return false;
        }
        return true;
    }

    std::unordered_map<std::string, usize> names;
    std::unordered_map<std::string, usize> folders;
    std::unordered_map<std::string, usize> paths;
    std::unordered_map<std::string, usize> runtimePaths;
    std::unordered_map<std::string, usize> runtimeHashes;
    std::vector<usize> groupParents;
    std::vector<Visit_State> visits;
};

} // namespace

std::optional<Module_Link_Plan> Prepare_Link(Runtime& runtime, std::span<const std::string> paths) {
    Module_Set selected(runtime);
    Module_Link_Plan plan;
    for (const std::string& path : paths) {
        if (runtime.progress != nullptr) {
            (*runtime.progress)(Runtime_Stage::Preparing, Path_File_Name(path.c_str()), 0, 0);
        }

        if (!selected.Read(path)) {
            return std::nullopt;
        }
    }

    if (selected.modules.empty()) {
        return plan;
    }

    std::vector<usize> roots(selected.modules.size());
    for (usize index = 0; index < roots.size(); index++) {
        roots[index] = index;
    }
    
    std::sort(roots.begin(), roots.end(), [&](usize left, usize right) {
        return selected.modules[left].manifest.name < selected.modules[right].manifest.name;
    });

    for (usize index : roots) {
        if (!selected.Visit(index)) {
            return std::nullopt;
        }
    }

    plan.runtimes = std::move(selected.runtimes);
    for (const auto& order : selected.Groups()) {
        auto& group = plan.groups.emplace_back();
        group.reserve(order.size());
        for (usize index : order) {
            group.push_back(std::move(selected.modules[index]));
        }
    }
    
    return plan;
}
