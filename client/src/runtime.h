#pragma once

#include "base.h"
#include "module_execution.h"
#include "module_manifest.h"
#include "platform.h"
#include "textures.h"
#include "network/lobby_client.h"
#include "network/lobby_server.h"
#include "modloader_debug.h"
#include "modloader_platform.h"
#include "wasm_export.h"
#include <unordered_map>

class Runtime_Host {
public:
    virtual void Invalidate_Code(u32, u64, u64) {}

    virtual bool Translate_Address(u32, u64, u32&, u64&) {
        return false;
    }

    virtual s64 Platform_Query(u32, void*, u64) {
        return -1;
    }

    virtual s32 Set_Texture_Sources(std::span<const char* const>, u32) {
        return MODLOADER_TEXTURE_UNSUPPORTED;
    }

    virtual void Request_Quit() {}
    virtual void Set_Breakpoints(std::span<const ModLoader_Breakpoint>, u32) {}

    virtual bool Has_Breakpoints() const {
        return false;
    }

    virtual bool Quitting() const {
        return false;
    }

    virtual bool Resize_Image(u64) {
        return false;
    }

    virtual u64 Peek_Memory(u32, u64, std::span<u8>) {
        return 0;
    }

    virtual u64 Poke_Memory(u32, u64, std::span<const u8>) {
        return 0;
    }

    virtual void Pause_Emulation(bool) {}

    virtual bool Emulation_Paused() const {
        return false;
    }

    virtual void Restart_Emulation() {}

protected:
    ~Runtime_Host() = default;
};

struct Runtime_Config {
    Platform* platform = nullptr;
    std::vector<ModLoader_Space_Descriptor> spaces;
    std::vector<ModLoader_Processor_Descriptor> processors;
    const Game_Image* image = nullptr;
    std::string dataDirectory;
    std::string aotCompiler;
    Module_Execution moduleExecution = Module_Execution::O0;
    u32 session = 0;
    std::optional<Lobby_Client::Config> lobbyConfig;
    std::optional<Lobby_Server::Config> serverConfig;
    bool ui = false;
    Runtime_Host* host = nullptr;
};

struct Linked_Module {
    std::string path;
    std::string name;
    std::vector<Module_Manifest> manifests;
};

enum class Runtime_Stage {
    Preparing,
    Linking,
    Compiling,
    Loading,
};

using Runtime_Progress = std::function<void(Runtime_Stage stage, const char* name, u32 index, u32 count)>;

struct Runtime_Symbol {
    u64 address;
    Module* definer;
};

struct Hypercall_Entry {
    Module* owner;
    u64 handler;
    bool registered;
};

struct Module_Breakpoint {
    u32 id;
    Module* owner;
    u64 handler;
    u64 start;
    u64 end;
    u32 access;
    u32 processor;
};

struct Module_Thread {
    wasm_thread_t tid;
    Module* module;
    u64 context;
};

class Runtime {
public:
    static std::unique_ptr<Runtime> Create(Runtime_Config config);
    ~Runtime();

    void Load_Modules(std::span<const std::string> paths, const Runtime_Progress& progress);
    void Register_Platform_Natives(std::string_view identifier);

    void Enter_Thread();
    void Leave_Thread();
    void Init_Modules();
    void Shutdown_Modules();
    void Event(u32 event, u64 time, const void* data, u64 size);
    s32 Hypercall(u32 processor, u32 hypercall_id, void* state, u64 size);
    void Breakpoint(const ModLoader_Break& hit, void* state, u64 size);
    void Hold(u32 reason, const std::function<bool()>& held);
    void Notify(u32 event);

    void Ui();
    void Stop_Ui_Instance(Module& module);
    u32 Ui_Event(Module& module, u64 listener, const void* record, u64 size);

    bool Resize_Image(u64 size);
    const Module* Savestates_Blocker() const;
    std::optional<u64> Symbol_Find(std::string_view name) const;

    bool Guest_Read(u64 address, void* out_data, u64 size);
    bool Guest_Write(u64 address, const void* data, u64 size);

    bool Map_Address(u64 address, u32& out_space, u64& out_offset, u32 processor = 0) const;
    void Invalidate(u64 address, u64 size, u32 processor = 0);
    u32 Allocate_Hypercall_Id();
    void Deliver(u32 event, const void* record, u64 record_size);
    void Subscribe(Module& module, u32 event, bool enabled);
    void Request_Dispatch(Module& module);
    void Dispatch_Pending();

    const ModLoader_Space_Descriptor& Ram() const {
        return config.spaces[ramSpace];
    }

    Runtime_Host& Host() const {
        return *config.host;
    }

    bool On_Emulation_Thread() const;
    bool Configure_Network(u16 port);

    Lobby_Client* Lobby() const {
        return activeLobby.load();
    }

    Lobby_Server* Server() const {
        return activeServer.load();
    }

    Runtime_Config config;
    Platform& platform;
    std::vector<std::unique_ptr<Module>> modules;
    bool modulesPrepared = false;
    wasm_shared_heap_t heapChain = nullptr;
    std::vector<u64> windows;
    std::unordered_map<std::string, u32> spaceIndices;
    u32 ramSpace = 0;
    std::unordered_map<u32, Hypercall_Entry> hypercalls;
    u32 nextHypercallId = 1;
    std::unordered_map<std::string, Runtime_Symbol> symbols;
    Texture_Manager textures;
    std::unordered_map<u32, std::shared_ptr<std::vector<Module*>>> subscribers;
    std::vector<Module*> uiModules;
    std::vector<Module*> dirtyModules;
    std::mutex dispatchLock;
    std::vector<Module*> pendingDispatch;
    u64 emulationThreadId = 0;
    u64 eventTime = 0;
    usize networkCursor = 0;
    bool networkDispatching = false;
    std::span<const u8> lobbyPayload;
    bool imageResized = false;
    u64 imageSize = 0;
    std::unordered_map<u32, std::unique_ptr<Module_Thread>> threads;
    u32 nextThreadId = 0;
    std::mutex threadLock;
    std::mutex uiLock;
    std::vector<Module_Breakpoint> breakpoints;
    u32 nextBreakpointId = 0;
    Module* stepOwner = nullptr;
    u64 stepHandler = 0;
    u32 stepProcessor = UINT32_MAX;
    u32 pausedProcessor = UINT32_MAX;
    u64 pausedStateSize = 0;
    void* pausedState = nullptr;
    bool paused = false;
    const Runtime_Progress* progress = nullptr;
    std::string aotCompilerFingerprint;

private:
    Runtime(Runtime_Config config);
    void Load_Linked_Module(Linked_Module module);
    void Start_Network();
    std::unique_ptr<Lobby_Client> ownedLobby;
    std::unique_ptr<Lobby_Server> ownedServer;
    std::atomic<Lobby_Client*> activeLobby = nullptr;
    std::atomic<Lobby_Server*> activeServer = nullptr;
    u16 defaultNetworkPort = 0;
    bool networkStarted = false;
    void Flush_Dirty_Pages();
    std::unique_ptr<Module> Load_Definition(const std::string& path, const std::string& name);
    bool Instantiate(Module& module);
    void Prepare_Modules();
    void Release(Module& module);
    void Start_Ui_Instance(Module& module);
};
