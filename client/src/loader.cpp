#include "module.h"

#include "modloader_module_exports.h"
#include "modloader_platform.h"

#include <algorithm>
#include <string.h>

namespace {

bool Bind_Exports(Module& module) {
    const Runtime& runtime = module.runtime;
    s32 export_count = wasm_runtime_get_export_count(module.module);
    bool ok = true;
    constexpr std::string_view component_prefix = "modloader_component.";

    for (s32 index = 0; index < export_count; index++) {
        wasm_export_t exported = {};
        wasm_runtime_get_export_type(module.module, index, &exported);
        std::string_view name = exported.name != nullptr ? exported.name : "";
        if (exported.kind == WASM_IMPORT_EXPORT_KIND_FUNC) {
            if (name.starts_with(component_prefix)) {
                if (!Module_Name_Is_Valid(name.substr(component_prefix.size())) ||
                    wasm_func_type_get_param_count(exported.u.func_type) != 0 || wasm_func_type_get_result_count(exported.u.func_type) != 0) {
                    Log_Error(module.name.c_str(), "invalid component %s", exported.name);
                    ok = false;
                }
                else {
                    module.components.emplace_back(name.substr(component_prefix.size()));
                }
            }
            else if (name.starts_with(MODLOADER_DIRTY_PREFIX)) {
                auto space = runtime.spaceIndices.find(std::string(name.substr(sizeof(MODLOADER_DIRTY_PREFIX) - 1)));
                wasm_function_inst_t accessor = wasm_runtime_lookup_function(module.instance, exported.name);
                wasm_val_t result = Wasm_I64(0);
                if (space != runtime.spaceIndices.end() && module.Call(accessor, exported.name, {}, &result) && result.of.i64 != 0) {
                    module.dirtyMaps.push_back({ space->second, static_cast<u64>(result.of.i64) });
                }
            }
            continue;
        }

        bool window = name.starts_with(MODLOADER_WINDOW_PREFIX);
        if (exported.kind != WASM_IMPORT_EXPORT_KIND_GLOBAL || (!window && !name.starts_with(MODLOADER_SYMBOL_PREFIX))) {
            continue;
        }

        wasm_global_inst_t global = {};
        if (!wasm_runtime_get_export_global_inst(module.instance, exported.name, &global) || global.global_data == nullptr ||
            (global.kind != WASM_I64 && (window || global.kind != WASM_I32))) {
            Log_Error(module.name.c_str(), "invalid guest global %s", exported.name);
            ok = false;
            continue;
        }

        module.guestGlobals.push_back({ exported.name, global });
        if (!window) {
            continue;
        }

        std::string_view rest = name.substr(sizeof(MODLOADER_WINDOW_PREFIX) - 1);
        usize suffix = rest.rfind('.');
        auto location = runtime.spaceIndices.find(std::string(rest.substr(0, suffix)));
        bool found = suffix != std::string_view::npos && location != runtime.spaceIndices.end() && (rest.substr(suffix) == ".base" || rest.substr(suffix) == ".size");
        if (found) {
            u32 space_index = location->second;
            const ModLoader_Space_Descriptor& space = runtime.config.spaces[space_index];
            found = (space.flags & MODLOADER_SPACE_MAPPED) != 0;
            if (found) {
                *static_cast<u64*>(global.global_data) = rest.ends_with(".base") ? runtime.windows[space_index] : space.usableSize;
            }
        }

        // Server-only
        if (!found && (runtime.config.session & MODLOADER_SESSION_GAME) != 0) {
            Log_Error(module.name.c_str(), "%s unavailable on %s", exported.name, runtime.platform.identifier.c_str());
            ok = false;
        }
    }

    std::ranges::sort(module.components);
    if (module.components.size() != module.manifests.size()) {
        ok = false;
    }

    for (const auto& manifest : module.manifests) {
        if (!std::ranges::binary_search(module.components, manifest.name)) {
            Log_Error(module.name.c_str(), "missing component %s", manifest.name.c_str());
            ok = false;
        }
    }

    if (ok) {
        // Manifests are already ordered with dependencies first.
        for (const Module_Manifest& manifest : module.manifests) {
            std::string name = std::string(component_prefix) + manifest.name;
            if (!module.Call(wasm_runtime_lookup_function(module.instance, name.c_str()), name.c_str())) {
                return false;
            }
        }
    }

    return ok;
}

} // namespace

std::unique_ptr<Module> Runtime::Load_Definition(const std::string& path, const std::string& name) {
    auto module = std::make_unique<Module>(*this, name);
    std::optional<std::vector<u8>> bytes = File_Read(path);
    char error[256] = {};

    if (!bytes) {
        Log_Error("runtime", "cannot read %s", path.c_str());
        return nullptr;
    }

    module->bytes = std::move(*bytes);
    if (config.moduleExecution != Module_Execution::Interpreter && !Aot_Take_Native(*this, *module)) {
        return nullptr;
    }

    if (module->bytes.size() > UINT32_MAX) {
        Log_Error(name.c_str(), "module binary exceeds max file size");
        return nullptr;
    }

    LoadArgs arguments = {};
    arguments.no_resolve = true;
    module->module = wasm_runtime_load_ex(module->bytes.data(), static_cast<u32>(module->bytes.size()), &arguments, error, sizeof(error));
    if (module->module == nullptr) {
        Log_Error(name.c_str(), "cannot load: %s", error);
        return nullptr;
    }
    return module;
}

bool Runtime::Instantiate(Module& loaded) {
    Module* module = &loaded;
    char error[256] = {};

    if (!wasm_runtime_resolve_symbols(module->module)) {
        Log_Error(module->name.c_str(), "cannot resolve module imports");
        return false;
    }

    module->instance = wasm_runtime_instantiate(module->module, gModuleExecStackSize, 0, error, sizeof(error));
    if (module->instance == nullptr) {
        Log_Error(module->name.c_str(), "cannot instantiate: %s", error);
        return false;
    }

    if (heapChain != nullptr && !wasm_runtime_attach_shared_heap(module->instance, heapChain)) {
        Log_Error(module->name.c_str(), "cannot map guest memory");
        return false;
    }

    wasm_runtime_set_custom_data(module->instance, module);
    module->execEnv = wasm_runtime_create_exec_env(module->instance, gModuleExecStackSize);
    if (module->execEnv == nullptr) {
        Log_Error(module->name.c_str(), "cannot create execution context");
        return false;
    }

    module->dispatch = wasm_runtime_lookup_function(module->instance, "modloader_dispatch");
    module->eventInit = wasm_runtime_lookup_function(module->instance, "modloader_event_init");
    module->eventShutdown = wasm_runtime_lookup_function(module->instance, "modloader_event_shutdown");
    module->eventPhase = wasm_runtime_lookup_function(module->instance, "modloader_event_phase");
    module->eventHypercall = wasm_runtime_lookup_function(module->instance, "modloader_event_hypercall");
    module->eventBreakpoint = wasm_runtime_lookup_function(module->instance, "modloader_event_breakpoint");
    module->eventReserve = wasm_runtime_lookup_function(module->instance, "modloader_event_buffer");
    if (module->eventReserve == nullptr || module->eventPhase == nullptr) {
        module->disabled = true;
        Log_Error(module->name.c_str(), "missing runtime exports");
        return false;
    }

    if (!Bind_Exports(*module)) {
        module->disabled = true;
        return false;
    }

    Files_Create_Folders(*module);
    return true;
}

void Runtime::Prepare_Modules() {
    if (modulesPrepared) {
        return;
    }

    for (const auto& module : modules) {
        if (!module->disabled && module->instance == nullptr) {
            if (!Instantiate(*module)) {
                module->disabled = true;
            }
            else if (!module->dirtyMaps.empty()) {
                dirtyModules.push_back(module.get());
            }
        }
    }

    modulesPrepared = true;
}

void Runtime::Load_Modules(std::span<const std::string> paths, const Runtime_Progress& report) {
    wasm_runtime_init_thread_env();
    progress = report ? &report : nullptr;
    auto linked = Link_Modules(*this, paths);
    if (!linked) {
        progress = nullptr;
        wasm_runtime_destroy_thread_env();
        return;
    }

    Aot_Compile_All(*this, *linked);
    for (usize index = 0; index < linked->size(); index++) {
        Linked_Module& module = (*linked)[index];
        if (progress != nullptr) {
            report(Runtime_Stage::Loading, module.name.c_str(), static_cast<u32>(index + 1), static_cast<u32>(linked->size()));
        }
        Load_Linked_Module(std::move(module));
    }

    Prepare_Modules();
    progress = nullptr;
    wasm_runtime_destroy_thread_env();
}

void Runtime::Load_Linked_Module(Linked_Module linked) {
    std::unique_ptr<Module> module = Load_Definition(linked.path, linked.name);
    if (module == nullptr) {
        return;
    }

    module->order = modules.size();
    module->manifests = std::move(linked.manifests);
    modulesPrepared = false;
    modules.push_back(std::move(module));
}
