#include "module.h"
#include <modloader_bytes.h>

#include "modloader_events.h"
#include "modloader_module_exports.h"
#include "modloader_platform.h"

#include <algorithm>

namespace {

void Write_Log(Module& module, u64 level, u64 pointer, u64 length, u64 source_pointer = 0, u64 source_length = 0) {
    auto message = module.Text(pointer, length);
    auto source = module.Text(source_pointer, source_length);
    if (!message || !source) {
        return;
    }
    
    const char* name = "modloader-runtime";
    if (!source->empty()) {
        if (*source == "modloader-runtime") {
            name = "modloader-runtime";
        }
        else {
            auto found = std::lower_bound(module.components.begin(), module.components.end(), *source,
                [](const std::string& component, std::string_view candidate) { return component < candidate; });
            if (found == module.components.end() || *found != *source) {
                return;
            }
            name = found->c_str();
        }
    }

    auto severity = level <= static_cast<u64>(Log_Level::Debug) ? static_cast<Log_Level>(level) : Log_Level::Info;
    Log_Write(severity, name, "%.*s", static_cast<int>(std::min(message->size(), usize(INT32_MAX))), message->data());
}

void Platform_Describe(Module& module, u64 pointer, u64 size) {
    const Runtime_Config& config = module.runtime.config;
    auto* destination = size >= sizeof(ModLoader_Platform_Description) ? module.Memory_As<ModLoader_Platform_Description>(pointer) : nullptr;
    if (destination == nullptr) {
        return;
    }

    ModLoader_Platform_Description description = {};
    description.abiVersion = MODLOADER_MODULE_ABI_VERSION;
    description.session = config.session;
    description.spaceCount = static_cast<u32>(config.spaces.size());
    description.processorCount = static_cast<u32>(config.processors.size());
    Text_Copy(description.platform, module.runtime.platform.identifier);
    *destination = description;
}

s32 Space_Describe(Module& module, u32 index, u64 pointer, u64 size) {
    auto* destination = size >= sizeof(ModLoader_Module_Space) ? module.Memory_As<ModLoader_Module_Space>(pointer) : nullptr;
    if (destination == nullptr || index >= module.runtime.config.spaces.size()) {
        return -1;
    }

    const auto& space = module.runtime.config.spaces[index];
    *destination = {};
    Text_Copy(destination->name, space.name);
    destination->size = space.size;
    destination->usableSize = space.usableSize;
    destination->reportedSize = space.reportedSize;
    destination->codec = space.codec;
    destination->flags = space.flags;
    return 0;
}

s32 Processor_Describe(Module& module, u32 index, u64 pointer, u64 size) {
    auto* destination = size >= sizeof(ModLoader_Module_Processor) ? module.Memory_As<ModLoader_Module_Processor>(pointer) : nullptr;
    if (destination == nullptr || index >= module.runtime.config.processors.size()) {
        return -1;
    }

    const auto& processor = module.runtime.config.processors[index];
    *destination = {};
    Text_Copy(destination->name, processor.name);
    Text_Copy(destination->stateFormat, processor.stateFormat);
    destination->stateSize = processor.stateSize;
    return 0;
}

void Symbol_Define(Module& module, u64 name, u64 length, u64 address) {
    Runtime& runtime = module.runtime;
    std::optional<std::string_view> text = module.Text(name, length);

    if (!text || text->empty()) {
        return;
    }

    auto [found, inserted] = runtime.symbols.emplace(std::string(*text), Runtime_Symbol { address, &module });
    if (!inserted && found->second.address != address) {
        Log_Warning(module.name.c_str(), "symbol %.*s: 0x%llX conflicts with %s (0x%llX)",
            static_cast<int>(text->size()), text->data(), static_cast<unsigned long long>(address),
            found->second.definer->name.c_str(), static_cast<unsigned long long>(found->second.address));
    }
}

bool Bind_Symbols(Module& module) {
    const Runtime& runtime = module.runtime;
    std::string_view name;
    std::optional<u64> address;
    bool missing = false;

    if ((runtime.config.session & MODLOADER_SESSION_GAME) == 0) {
        return true;
    }

    for (const Module_Guest_Global& binding : module.guestGlobals) {
        name = binding.name;
        if (!name.starts_with(MODLOADER_SYMBOL_PREFIX)) {
            continue;
        }

        const wasm_global_inst_t& global = binding.global;
        name.remove_prefix((sizeof(MODLOADER_SYMBOL_PREFIX) - 1));
        address = runtime.Symbol_Find(name);
        if (!address) {
            Log_Error(module.name.c_str(), "missing guest symbol %.*s", static_cast<int>(name.size()), name.data());
            missing = true;
            continue;
        }

        if (global.kind == WASM_I64) {
            *static_cast<u64*>(global.global_data) = *address;
        }
        else {
            *static_cast<u32*>(global.global_data) = static_cast<u32>(*address);
        }
    }

    if (missing) {
        module.disabled = true;
        return false;
    }

    return true;
}

bool Guest_Copy(Module& module, u64 destination, u64 source, u64 size) {
    std::vector<u8> buffer;

    if (size > SIZE_MAX) {
        return false;
    }

    buffer.resize(size);
    return size == 0 || (module.runtime.Guest_Read(source, buffer.data(), size) && module.runtime.Guest_Write(destination, buffer.data(), size));
}

u64 Translate_Address(Module& module, u32 processor, u64 address) {
    u32 space;
    u64 offset;

    return module.runtime.Map_Address(address, space, offset, processor) ? module.runtime.platform.Fixed_Address(processor, space, offset) : 0;
}

u32 Hypercall_Allocate(Module& module) {
    u32 id = module.runtime.Allocate_Hypercall_Id();

    if (id != 0) {
        module.runtime.hypercalls.emplace(id, Hypercall_Entry { &module, 0, false });
    }

    return id;
}

bool Hypercall_Register(Module& module, u32 id, u64 handler) {
    auto found = module.runtime.hypercalls.find(id);
    if (found != module.runtime.hypercalls.end() && found->second.owner == &module) {
        found->second.handler = handler;
        found->second.registered = true;
        return true;
    }

    return false;
}

u64 Savestate_Block_Create(Module& module, u64 owner, u64 owner_length, u64 reason, u64 length) {
    auto source = module.Text(owner, owner_length);
    auto text = module.Text(reason, length);
    if (!source || !text || module.nextSavestateBlock == UINT64_MAX ||
        (*source != "modloader-runtime" &&
            !std::binary_search(module.components.begin(), module.components.end(), *source))) {
        return 0;
    }

    u64 token = ++module.nextSavestateBlock;
    module.savestateBlocks.emplace(token, Savestate_Block{std::string(*source), std::string(*text)});
    return token;
}

u64 Flat_Load(wasm_exec_env_t exec_env, u64 address, u32 size) {
    Runtime& runtime = Module_Of(exec_env).runtime;
    u8 bytes[8] = {};

    if (size > 8 || !runtime.Guest_Read(address, bytes, size)) {
        wasm_runtime_set_exception(wasm_runtime_get_module_inst(exec_env), "guest load from an unmapped address");
        return 0;
    }

    return ModLoader::Bytes::Read_Integer(bytes, size, runtime.platform.bigEndian);
}

void Flat_Store(wasm_exec_env_t exec_env, u64 address, u32 size, u64 value) {
    Runtime& runtime = Module_Of(exec_env).runtime;
    u8 bytes[8] = {};

    if (size > sizeof(bytes)) {
        wasm_runtime_set_exception(wasm_runtime_get_module_inst(exec_env), "invalid guest store size");
        return;
    }
    
    ModLoader::Bytes::Write_Integer(bytes, value, size, runtime.platform.bigEndian);
    if (!runtime.Guest_Write(address, bytes, size)) {
        wasm_runtime_set_exception(wasm_runtime_get_module_inst(exec_env), "guest store to an unmapped or read-only address");
    }
}

NativeSymbol sNatives[] = {
    Native("log", "(iII)", [](wasm_exec_env_t exec_env, u64* slots) {
        Write_Log(Module_Of(exec_env), slots[0], slots[1], slots[2]);
    }),
    Native("log_source", "(iIIII)", [](wasm_exec_env_t exec_env, u64* slots) {
        Write_Log(Module_Of(exec_env), slots[0], slots[3], slots[4], slots[1], slots[2]);
    }),
    Native("abort_with", "(II)", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        std::optional<std::string_view> message = module.Text(slots[0], slots[1]);

        if (message) {
            Log_Error(module.name.c_str(), "%.*s", static_cast<int>(message->size()), message->data());
        }
        wasm_runtime_set_exception(wasm_runtime_get_module_inst(exec_env), "aborted");
    }),
    Native("platform_describe", "(II)", [](wasm_exec_env_t exec_env, u64* slots) {
        Platform_Describe(Module_Of(exec_env), slots[0], slots[1]);
    }),
    Native("platform_space_describe", "(iII)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = static_cast<u64>(Space_Describe(Module_Of(exec_env), static_cast<u32>(slots[0]), slots[1], slots[2]));
    }),
    Native("platform_processor_describe", "(iII)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = static_cast<u64>(Processor_Describe(Module_Of(exec_env), static_cast<u32>(slots[0]), slots[1], slots[2]));
    }),
    Native("platform_query", "(iII)I", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        void* output = slots[2] != 0 ? module.Memory(slots[1], slots[2]) : nullptr;
        slots[0] = static_cast<u64>((slots[2] == 0 || output != nullptr) && Require_Emulation_Thread(exec_env, "platform_query")
            ? module.runtime.platform.Query(module.runtime, static_cast<u32>(slots[0]), output, slots[2]) : -1);
    }),
    Native("dispatch_request", "()", [](wasm_exec_env_t exec_env, u64*) {
        Module& module = Module_Of(exec_env);
        module.runtime.Request_Dispatch(module);
    }),
    Native("symbol_define", "(III)", [](wasm_exec_env_t exec_env, u64* slots) {
        Symbol_Define(Module_Of(exec_env), slots[0], slots[1], slots[2]);
    }),
    Native("symbol_address", "(II)I", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        std::optional<std::string_view> name = module.Text(slots[0], slots[1]);

        slots[0] = name ? module.runtime.Symbol_Find(*name).value_or(0) : 0;
    }),
    Native("bind_symbols", "()i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Bind_Symbols(Module_Of(exec_env)) ? 1 : 0;
    }),
    Native("event_subscribe", "(ii)", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        if (Require_Emulation_Thread(exec_env, "event_subscribe")) {
            module.runtime.Subscribe(module, static_cast<u32>(slots[0]), slots[1] != 0);
        }
    }),
    Native("clock_now", "(i)I", [](wasm_exec_env_t exec_env, u64* slots) {
        const Platform& platform = Module_Of(exec_env).runtime.platform;
        u32 clock = static_cast<u32>(slots[0]);

        slots[0] = clock == MODLOADER_CLOCK_MILLISECONDS ? Time_Monotonic_Milliseconds() : clock <= platform.clockCount ? platform.Clock_Now(clock) : 0;
    }),
    Native("invalidate_code", "(II)", [](wasm_exec_env_t exec_env, u64* slots) {
        if (Require_Emulation_Thread(exec_env, "ModLoader::Guest::Invalidate_Code")) {
            Module_Of(exec_env).runtime.Invalidate(slots[0], slots[1]);
        }
    }),
    Native("image_resize", "(I)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Module_Of(exec_env).runtime.Resize_Image(slots[0]) ? 0 : static_cast<u64>(-1);
    }),
    Native("memory_peek", "(iIII)I", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        u8* buffer = slots[3] != 0 ? module.Memory_As<u8>(slots[2], slots[3]) : nullptr;
        slots[0] = buffer != nullptr && Require_Emulation_Thread(exec_env, "ModLoader::Guest::Peek")
            ? module.runtime.Host().Peek_Memory(static_cast<u32>(slots[0]), slots[1], { buffer, static_cast<usize>(slots[3]) }) : 0;
    }),
    Native("memory_poke", "(iIII)I", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        const u8* data = slots[3] != 0 ? module.Memory_As<u8>(slots[2], slots[3]) : nullptr;
        u32 processor = static_cast<u32>(slots[0]);
        u64 written = data != nullptr && Require_Emulation_Thread(exec_env, "ModLoader::Guest::Poke")
            ? module.runtime.Host().Poke_Memory(processor, slots[1], { data, static_cast<usize>(slots[3]) }) : 0;
        module.runtime.Invalidate(slots[1], written, processor);
        slots[0] = written;
    }),
    Native("guest_copy", "(III)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Require_Emulation_Thread(exec_env, "ModLoader::Guest::Copy") && Guest_Copy(Module_Of(exec_env), slots[0], slots[1], slots[2]) ? 1 : 0;
    }),
    Native("translate_address", "(iI)I", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Require_Emulation_Thread(exec_env, "ModLoader::gPlatform.Translate") ? Translate_Address(Module_Of(exec_env), static_cast<u32>(slots[0]), slots[1]) : 0;
    }),
    Native("hypercall_allocate", "()i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Require_Emulation_Thread(exec_env, "Hypercall::Allocate") ? Hypercall_Allocate(Module_Of(exec_env)) : 0;
    }),
    Native("hypercall_register", "(iI)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Require_Emulation_Thread(exec_env, "Hypercall::Register") &&
                Hypercall_Register(Module_Of(exec_env), static_cast<u32>(slots[0]), slots[1])
            ? 1
            : 0;
    }),
    Native("savestate_block", "(IIII)I", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Require_Emulation_Thread(exec_env, "Savestate::Block")
            ? Savestate_Block_Create(Module_Of(exec_env), slots[0], slots[1], slots[2], slots[3]) : 0;
    }),
    Native("savestate_unblock", "(I)", [](wasm_exec_env_t exec_env, u64* slots) {
        if (Require_Emulation_Thread(exec_env, "ModLoader::Savestate")) {
            Module_Of(exec_env).savestateBlocks.erase(slots[0]);
        }
    }),
    Native("emulation_pause", "(i)", [](wasm_exec_env_t exec_env, u64* slots) {
        if (Require_Emulation_Thread(exec_env, "Emulation::Pause")) {
            Module_Of(exec_env).runtime.Host().Pause_Emulation(slots[0] != 0);
        }
    }),
    Native("emulation_paused", "()i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Module_Of(exec_env).runtime.Host().Emulation_Paused() ? 1 : 0;
    }),
    Native("emulation_restart", "()", [](wasm_exec_env_t exec_env, u64*) {
        if (Require_Emulation_Thread(exec_env, "Emulation::Restart")) {
            Module_Of(exec_env).runtime.Host().Restart_Emulation();
        }
    }),
    Native("flat_load", "(Ii)I", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Require_Emulation_Thread(exec_env, "a guest load") ? Flat_Load(exec_env, slots[0], static_cast<u32>(slots[1])) : 0;
    }),
    Native("flat_store", "(IiI)", [](wasm_exec_env_t exec_env, u64* slots) {
        if (Require_Emulation_Thread(exec_env, "a guest store")) {
            Flat_Store(exec_env, slots[0], static_cast<u32>(slots[1]), slots[2]);
        }
    }),
};

} // namespace

void Core_Register_Natives() {
    Register_Natives(sNatives, "modloader");
}
