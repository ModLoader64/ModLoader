#pragma once

#include "runtime.h"

#include <atomic>
#include <unordered_set>

constexpr u32 gModuleExecStackSize = 16 * 1024;

struct Module_File;
struct Module_Socket;

struct Module_Guest_Global {
    const char* name;
    wasm_global_inst_t global;
};

struct Module_Dirty_Map {
    u32 space;
    u64 address;
};

struct Module_Context {
    wasm_exec_env_t execEnv = nullptr;
    u64 threadRecord = 0;
};

struct Savestate_Block {
    std::string owner;
    std::string reason;
};

struct Module {
    Runtime& runtime;
    std::string name;
    bool initialized = false;
    bool released = false;
    std::vector<u8> bytes;
    wasm_module_t module = nullptr;
    wasm_module_inst_t instance = nullptr;
    wasm_exec_env_t execEnv = nullptr;
    wasm_function_inst_t dispatch = nullptr;
    bool dispatchPending = false; // protected by runtime.dispatchLock
    wasm_function_inst_t eventInit = nullptr;
    wasm_function_inst_t eventShutdown = nullptr;
    wasm_function_inst_t eventPhase = nullptr;
    wasm_function_inst_t eventHypercall = nullptr;
    wasm_function_inst_t eventBreakpoint = nullptr;
    Module_Context uiContext;
    wasm_function_inst_t uiPhase = nullptr;
    wasm_function_inst_t uiEvent = nullptr;
    wasm_function_inst_t uiEventReserve = nullptr;
    wasm_function_inst_t eventReserve = nullptr;
    usize order = 0;
    std::vector<u32> events;
    std::unordered_set<std::string> lobbyPackets;
    std::unordered_set<std::string> serverPackets;
    std::vector<Module_Dirty_Map> dirtyMaps;
    std::vector<Module_Guest_Global> guestGlobals; // names/data owned by the main module instance
    std::vector<Module_Layout_Slot> layoutSlots;
    std::unordered_map<wasm_module_inst_t, Module_Layout_Binding> layoutBindings;
    std::vector<std::string> components;
    std::vector<Module_Manifest> manifests;
    std::unordered_map<u64, Savestate_Block> savestateBlocks;
    u64 nextSavestateBlock = 0;
    std::atomic<bool> disabled = false;
    std::atomic<bool> stopping = false;
    std::mutex resourceLock;
    std::unordered_map<u32, std::shared_ptr<Module_File>> files;
    std::unordered_map<u32, std::shared_ptr<Module_Socket>> sockets;
    u32 nextFile = 0;
    u32 nextSocket = 0;

    Module(Runtime& owner, std::string module_name);
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    ~Module();

    void* Memory(u64 address, u64 size) const; // Validated guest range, or null

    template <typename T>
    T* Memory_As(u64 address, u64 count = 1) const {
        if (count > UINT64_MAX / sizeof(T) || address % alignof(T) != 0) {
            return nullptr;
        }
        return static_cast<T*>(Memory(address, count * sizeof(T)));
    }

    std::optional<std::string_view> Text(u64 address, u64 length) const;
    const char* C_Text(u64 address) const;

    // Main instance only
    bool Call(wasm_function_inst_t function, const char* what, std::initializer_list<wasm_val_t> arguments = {}, wasm_val_t* result = nullptr);
    bool Execute(wasm_exec_env_t context, wasm_function_inst_t function, u32 result_count, wasm_val_t* results, u32 argument_count, wasm_val_t* arguments);
    void Trap(wasm_exec_env_t context, const char* what);
    bool Call_Handler(u64 handler, u32 processor, void* state, u64 size, bool& out_result);
    void* Event_Memory(u64 size);
    bool Follows(u32 event) const;
    void Deliver(u32 event, const void* record, u64 record_size, bool subscribed = true);
    void Deliver_Phase(u32 phase, u32 event, const void* record, u64 record_size);

    bool Adopt_Instance(wasm_module_inst_t spawned) const;
    bool Create_Context(wasm_exec_env_t caller, Module_Context& out);
    void Destroy_Context(Module_Context& context);

    const Module_Manifest* Manifest_For_Folder(std::string_view folder) const;
    std::optional<std::string> Owner_Folder(u64 folder_address, u64 folder_length) const;
    std::string Folder_Path(std::string_view folder) const;// <data>/mods/<folder>
};

// Cloned instances inherit the owning module as custom data
Module& Module_Of(wasm_exec_env_t exec_env);

inline wasm_val_t Wasm_I32(s32 value) {
    wasm_val_t result = {};

    result.kind = WASM_I32;
    result.of.i32 = value;
    return result;
}

inline wasm_val_t Wasm_I64(u64 value) {
    wasm_val_t result = {};

    result.kind = WASM_I64;
    result.of.i64 = static_cast<s64>(value);
    return result;
}

// Raw arguments use 64-bit slots; slots[0] holds the result
using Native_Function = void (*)(wasm_exec_env_t exec_env, u64* slots);

inline NativeSymbol Native(const char* name, const char* signature, Native_Function function) {
    return { name, reinterpret_cast<void*>(function), signature, nullptr };
}

void Register_Natives(std::span<NativeSymbol> natives, const char* what, const char* import_module = "modloader"); // WAMR retains and sorts the native table
bool Require_Emulation_Thread(wasm_exec_env_t exec_env, const char* what); // Trap and return false outside the emulation thread

void Core_Register_Natives();
void N64_Tools_Register_Natives();
void Files_Register_Natives();
void Net_Register_Natives();
void Threads_Register_Natives();
void Tasks_Register_Natives();
void Breakpoints_Register_Natives();
void Lobby_Register_Natives();
void Module_Settings_Register_Natives();
void Library_Register_Natives();
void Files_Create_Folders(const Module& module);
void Files_Release_Owner(Module& owner);
void Net_Release_Owner(Module& owner);
void Net_Dispatch(Runtime& runtime, u64 time);
void Threads_Release_Owner(Module& owner);
void Tasks_Release_Owner(Module& owner);
void Breakpoints_Release_Owner(Module& owner);
std::optional<std::string> Module_File_Path(const Module& module, std::string_view path);
std::optional<std::string> Module_Mounted_Path(const Module& module, std::string_view folder, std::string_view path);
void Aot_Compile_All(Runtime& runtime, std::span<const Linked_Module> modules);
std::optional<std::vector<Linked_Module>> Link_Modules(Runtime& runtime, std::span<const std::string> paths);
bool Aot_Take_Native(Runtime& runtime, Module& module);
void Lobby_Dispatch(Runtime& runtime, u64 time);
void Module_Settings_Dispatch(Runtime& runtime, u64 time);
