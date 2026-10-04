#include "module.h"
#include <algorithm>
#include <unordered_set>
#include "ui/imgui_bindings.h"
#include "ui/windows.h"
#include "modloader_events.h"
#include "modloader_module_exports.h"
#include <string.h>

namespace {

constexpr u64 gPageSize = 4096;

bool sWamrReady = false;
std::unordered_set<std::string> sRegisteredPlatforms;

void Register_Platform(Platform& platform) {
    if (sRegisteredPlatforms.insert(platform.identifier).second) {
        platform.Register_Natives();
    }
}

bool Start_Wamr(Platform& platform) {
    RuntimeInitArgs init_arguments = {};

    if (sWamrReady) {
        Register_Platform(platform);
        return true;
    }

    init_arguments.mem_alloc_type = Alloc_With_System_Allocator;
    if (!wasm_runtime_full_init(&init_arguments)) {
        Log_Error("runtime", "WAMR failed to start");
        return false;
    }

    Core_Register_Natives();
    N64_Tools_Register_Natives();
    Register_Platform(platform);
    Threads_Register_Natives();
    Tasks_Register_Natives();
    Files_Register_Natives();
    Net_Register_Natives();
    Lobby_Register_Natives();
    Module_Settings_Register_Natives();
    Library_Register_Natives();
    Textures_Register_Natives();
    Breakpoints_Register_Natives();
    Imgui_Register_Natives();
    Ui_Windows_Register_Natives();
    wasm_runtime_set_max_thread_num(UINT32_MAX - 1);
    wasm_runtime_set_log_level(Log_Get_Level() >= Log_Level::Debug ? WASM_LOG_LEVEL_WARNING : WASM_LOG_LEVEL_ERROR);
    sWamrReady = true;
    return true;
}

} // namespace

Runtime::Runtime(Runtime_Config runtime_config) : config(std::move(runtime_config)), platform(*config.platform), textures(*this) {}

void Runtime::Register_Platform_Natives(std::string_view identifier) {
    if (!sRegisteredPlatforms.contains(std::string(identifier))) {
        auto module_platform = Platform_Create(identifier);
        Register_Platform(*module_platform);
    }
}

std::unique_ptr<Runtime> Runtime::Create(Runtime_Config config) {
    std::unique_ptr<Runtime> runtime;
    u64 mapped_size = 0;
    bool has_ram = false;

    if (!Start_Wamr(*config.platform)) {
        return nullptr;
    }

    runtime.reset(new Runtime(std::move(config)));
    runtime->windows.resize(runtime->config.spaces.size());
    for (u32 index = 0; index < runtime->config.spaces.size(); index++) {
        runtime->spaceIndices.emplace(runtime->config.spaces[index].name, index);
        if (runtime->platform.ramSpace != nullptr && strcmp(runtime->config.spaces[index].name, runtime->platform.ramSpace) == 0) {
            runtime->ramSpace = index;
            has_ram = true;
        }
    }

    if (runtime->platform.ramSpace != nullptr && !has_ram) {
        Log_Error("runtime", "missing %s space for %s", runtime->platform.ramSpace, runtime->platform.identifier.c_str());
        return nullptr;
    }

    for (u32 index = 0; index < runtime->config.spaces.size(); index++) {
        const ModLoader_Space_Descriptor& space = runtime->config.spaces[index];
        SharedHeapInitArgs arguments = {};
        wasm_shared_heap_t heap;

        if ((space.flags & MODLOADER_SPACE_MAPPED) == 0) {
            continue;
        }

        if (space.size > UINT64_MAX - mapped_size) {
            return nullptr;
        }

        arguments.size = space.size;
        arguments.pre_allocated_addr = space.hostBase;
        heap = wasm_runtime_create_shared_heap(&arguments);
        if (heap != nullptr && runtime->heapChain != nullptr) {
            heap = wasm_runtime_chain_shared_heaps(heap, runtime->heapChain);
        }

        if (heap == nullptr) {
            Log_Error("runtime", "cannot map guest space %s", space.name);
            return nullptr;
        }

        runtime->heapChain = heap;
        mapped_size += space.size;
        runtime->windows[index] = 0 - mapped_size;
    }
    return runtime->platform.Start(*runtime) ? std::move(runtime) : nullptr;
}

Runtime::~Runtime() {
    modules.clear();
    ownedLobby.reset();
    ownedServer.reset();
}

bool Runtime::On_Emulation_Thread() const {
    return Thread_Current_Id() == emulationThreadId;
}

u32 Runtime::Allocate_Hypercall_Id() {
    return nextHypercallId != 0 && nextHypercallId <= platform.lastHypercall ? nextHypercallId++ : 0;
}

const Module* Runtime::Savestates_Blocker() const {
    for (const std::unique_ptr<Module>& module : modules) {
        if (!module->savestateBlocks.empty() && !module->disabled) {
            return module.get();
        }
    }
    return nullptr;
}

std::optional<u64> Runtime::Symbol_Find(std::string_view name) const {
    auto found = symbols.find(std::string(name));
    return found != symbols.end() ? std::optional(found->second.address) : std::nullopt;
}

void Runtime::Flush_Dirty_Pages() {
    for (Module* module : dirtyModules) {
        if (module->disabled) {
            continue;
        }

        for (const Module_Dirty_Map& dirty : module->dirtyMaps) {
            const ModLoader_Space_Descriptor& space = config.spaces[dirty.space];
            u64 page_count = space.size / gPageSize;
            u8* map = module->Memory_As<u8>(dirty.address, page_count);

            for (u64 page = 0; map != nullptr && page < page_count;) {
                if (map[page] == 0) {
                    page++;
                    continue;
                }

                u64 first = page;
                while (page < page_count && map[page] != 0) {
                    map[page++] = 0;
                }

                Host().Invalidate_Code(dirty.space, first * gPageSize, (page - first) * gPageSize);
            }
        }
    }
}

void Runtime::Release(Module& module) {
    module.stopping = true;
    module.initialized = false;
    module.released = true;
    {
        std::lock_guard lock(dispatchLock);
        std::erase(pendingDispatch, &module);
        module.dispatchPending = false;
    }

    Stop_Ui_Instance(module);
    while (!module.events.empty()) {
        Subscribe(module, module.events.back(), false);
    }

    std::erase(dirtyModules, &module);
    Net_Release_Owner(module);
    textures.Release(module);
    Tasks_Release_Owner(module);
    Threads_Release_Owner(module);
    Files_Release_Owner(module);
    Breakpoints_Release_Owner(module);

    std::erase_if(hypercalls, [&](const auto& entry) {
        return entry.second.owner == &module;
    });

    std::erase_if(symbols, [&](const auto& symbol) {
        return symbol.second.definer == &module;
    });
}

void Runtime::Enter_Thread() {
    wasm_runtime_init_thread_env();
    emulationThreadId = Thread_Current_Id();
    Prepare_Modules();
    for (const std::unique_ptr<Module>& module : modules) {
        if (module->instance == nullptr) {
            continue;
        }

        if (module->execEnv != nullptr) {
            wasm_runtime_destroy_exec_env(module->execEnv);
        }

        module->execEnv = wasm_runtime_create_exec_env(module->instance, gModuleExecStackSize);
        if (module->execEnv == nullptr) {
            Log_Error(module->name.c_str(), "cannot create execution context");
            module->disabled = true;
        }
    }
}

void Runtime::Leave_Thread() {
    for (const std::unique_ptr<Module>& module : modules) {
        if (module->execEnv != nullptr) {
            wasm_runtime_destroy_exec_env(module->execEnv);
            module->execEnv = nullptr;
        }
    }
    wasm_runtime_destroy_thread_env();
}

void Runtime::Init_Modules() {
    Prepare_Modules();
    for (const auto& module : modules) {
        module->initialized = module->Call(module->eventInit, "On_Init") && !module->disabled;
    }

    Start_Network();
    if (config.ui) {
        for (const auto& initialized : modules) {
            if (!initialized->disabled && initialized->Follows(MODLOADER_EVENT_UI)) {
                Start_Ui_Instance(*initialized);
            }
        }
    }

    Flush_Dirty_Pages();
}

void Runtime::Shutdown_Modules() {
    for (const std::unique_ptr<Module>& module : modules) {
        module->stopping = true;
    }

    for (auto iterator = modules.rbegin(); iterator != modules.rend(); iterator++) {
        const std::unique_ptr<Module>& module = *iterator;
        Net_Release_Owner(*module);
        Tasks_Release_Owner(*module);
        Stop_Ui_Instance(*module);
        if (module->initialized) {
            module->Call(module->eventShutdown, "On_Shutdown");
            module->initialized = false;
        }
    }

    Flush_Dirty_Pages();
    for (auto iterator = modules.rbegin(); iterator != modules.rend(); iterator++) {
        Release(**iterator);
    }

    textures.Dispatch();
}

void Runtime::Event(u32 event, u64 time, const void* data, u64 size) {
    std::vector<u8> record(sizeof(ModLoader_Event_Header));
    ModLoader_Event_Header header = { time, Time_Monotonic_Milliseconds() };

    eventTime = time;
    if (event == MODLOADER_EVENT_RESET) {
        platform.Reset();
    }
    else if (event == MODLOADER_EVENT_STATE_LOADED) {
        platform.State_Loaded();
    }
    else {
        platform.Event_Record(event, time, data, size, record);
    }

    if (record.size() >= sizeof(ModLoader_Event_Header)) {
        memcpy(record.data(), &header, sizeof(header));
        Deliver(event, record.data(), record.size());
    }

    if (imageResized) {
        ModLoader_Image_Resized_Record resized = { header, imageSize };
        imageResized = false;
        Deliver(MODLOADER_EVENT_IMAGE_RESIZED, &resized, sizeof(resized));
    }

    if (event == MODLOADER_EVENT_REFRESH) {
        textures.Dispatch();
        Net_Dispatch(*this, time);
        Lobby_Dispatch(*this, time);
        Module_Settings_Dispatch(*this, time);
    }

    Dispatch_Pending();
    Flush_Dirty_Pages();
}

void Runtime::Hold(u32 reason, const std::function<bool()>& held) {
    ModLoader_Pause_Record record = { { eventTime, Time_Monotonic_Milliseconds() }, reason, 0 };

    Deliver(MODLOADER_EVENT_PAUSE, &record, sizeof(record));
    Flush_Dirty_Pages();

    while (held() && !Host().Quitting()) {
        Time_Sleep_Milliseconds(16);
        Notify(MODLOADER_EVENT_PAUSED);
    }

    Notify(MODLOADER_EVENT_RESUME);
}

void Runtime::Notify(u32 event) {
    ModLoader_Event_Header header = { eventTime, Time_Monotonic_Milliseconds() };

    Deliver(event, &header, sizeof(header));
    textures.Dispatch();
    Net_Dispatch(*this, eventTime);
    Dispatch_Pending();
    Flush_Dirty_Pages();
}

void Runtime::Subscribe(Module& module, u32 event, bool enabled) {
    auto position = std::lower_bound(module.events.begin(), module.events.end(), event);
    bool present = position != module.events.end() && *position == event;

    if (present == enabled) {
        return;
    }

    auto& list = subscribers[event];
    if (!list) {
        list = std::make_shared<std::vector<Module*>>();
    }
    else if (list.use_count() != 1) {
        list = std::make_shared<std::vector<Module*>>(*list);
    }

    if (enabled) {
        module.events.insert(position, event);
        auto recipient = std::lower_bound(list->begin(), list->end(), module.order,
            [](const Module* candidate, usize order) {
                return candidate->order < order;
            });
        list->insert(recipient, &module);
    }
    else {
        module.events.erase(position);
        std::erase(*list, &module);
        if (list->empty()) {
            subscribers.erase(event);
        }
    }
}

void Runtime::Deliver(u32 event, const void* record, u64 record_size) {
    auto found = subscribers.find(event);

    if (found == subscribers.end()) {
        return;
    }

    auto recipients = found->second;
    for (u32 phase : { MODLOADER_EVENT_PHASE_PRE, MODLOADER_EVENT_PHASE_NORMAL }) {
        for (Module* module : *recipients) {
            module->Deliver_Phase(phase, event, record, record_size);
        }
    }

    for (auto recipient = recipients->rbegin(); recipient != recipients->rend(); recipient++) {
        (*recipient)->Deliver_Phase(MODLOADER_EVENT_PHASE_POST, event, record, record_size);
    }
}

void Runtime::Request_Dispatch(Module& module) {
    std::lock_guard lock(dispatchLock);
    if (!module.dispatchPending && !module.stopping && !module.disabled) {
        module.dispatchPending = true;
        pendingDispatch.push_back(&module);
    }
}

void Runtime::Dispatch_Pending() {
    std::vector<Module*> pending;
    {
        std::lock_guard lock(dispatchLock);
        pending.swap(pendingDispatch);
        for (Module* module : pending) {
            module->dispatchPending = false;
        }
    }

    for (Module* module : pending) {
        if (!module->stopping) {
            module->Call(module->dispatch, "pending callbacks");
        }
    }
}

s32 Runtime::Hypercall(u32 processor, u32 hypercall_id, void* state, u64 size) {
    if (processor >= config.processors.size() || size != config.processors[processor].stateSize || (size != 0 && state == nullptr)) {
        return 0;
    }

    auto found = hypercalls.find(hypercall_id);
    if (found != hypercalls.end() && found->second.registered) {
        const Hypercall_Entry entry = found->second;
        bool handled = false;
        if (!entry.owner->Call_Handler(entry.handler, processor, state, size, handled)) {
            return 0;
        }
        Flush_Dirty_Pages();
        return handled ? 1 : 0;
    }

    return 0;
}

bool Runtime::Resize_Image(u64 size) {
    std::string global_name;
    wasm_global_inst_t global;
    auto set_size = [&](wasm_module_inst_t instance) {
        if (instance != nullptr && wasm_runtime_get_export_global_inst(instance, global_name.c_str(), &global) && global.global_data != nullptr) {
            *static_cast<u64*>(global.global_data) = size;
        }
    };

    for (ModLoader_Space_Descriptor& space : config.spaces) {
        if (platform.imageSpace == nullptr || strcmp(space.name, platform.imageSpace) != 0) {
            continue;
        }

        if (size > space.size || !Host().Resize_Image(size)) {
            return false;
        }

        space.usableSize = size;
        global_name = Text_Format(MODLOADER_WINDOW_PREFIX "%s.size", space.name);
        for (const std::unique_ptr<Module>& module : modules) {
            set_size(module->instance);
            set_size(module->uiContext.execEnv != nullptr ? wasm_runtime_get_module_inst(module->uiContext.execEnv) : nullptr);
        }
        
        imageSize = size;
        imageResized = true;
        return true;
    }
    return false;
}
