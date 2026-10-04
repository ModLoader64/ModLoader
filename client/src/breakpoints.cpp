#include "module.h"

#include "modloader_debug.h"
#include "modloader_events.h"

#include <string.h>

namespace {

constexpr u32 gAccessMask = MODLOADER_BREAK_EXECUTE | MODLOADER_BREAK_READ | MODLOADER_BREAK_WRITE;

void Push(Runtime& runtime) {
    std::vector<ModLoader_Breakpoint> list;

    list.reserve(runtime.breakpoints.size());
    for (const Module_Breakpoint& breakpoint : runtime.breakpoints) {
        list.push_back({ breakpoint.start, breakpoint.end, breakpoint.access, breakpoint.id, breakpoint.processor, 0 });
    }
    runtime.Host().Set_Breakpoints(list, runtime.stepOwner != nullptr ? runtime.stepProcessor : UINT32_MAX);
}

// Event buffer: CPU state followed by the breakpoint record
bool Call_Handler(Module& module, u64 handler, const ModLoader_Break& hit, void* state, u64 size) {
    u8* buffer = size <= UINT64_MAX - sizeof(hit) ? static_cast<u8*>(module.Event_Memory(size + sizeof(hit))) : nullptr;
    wasm_val_t result = {};

    if (module.disabled || module.eventBreakpoint == nullptr || buffer == nullptr) {
        return false;
    }

    memcpy(buffer, state, size);
    memcpy(buffer + size, &hit, sizeof(hit));

    if (!module.Call(module.eventBreakpoint, "a breakpoint handler", { Wasm_I64(handler), Wasm_I32(hit.processor), Wasm_I64(size) }, &result)) {
        return false;
    }

    memcpy(state, buffer, size);
    return result.of.i32 != 0;
}

u32 Add(Module& module, u32 processor, u64 start, u64 size, u32 access, u64 handler) {
    Runtime& runtime = module.runtime;

    access &= gAccessMask;
    if (size == 0 || access == 0 || start + size < start || processor >= runtime.config.processors.size() || runtime.nextBreakpointId == UINT32_MAX || !runtime.Host().Has_Breakpoints()) {
        return 0;
    }

    runtime.breakpoints.push_back({ ++runtime.nextBreakpointId, &module, handler, start, start + size, access, processor });
    Push(runtime);
    return runtime.nextBreakpointId;
}

void Remove(Module& module, u32 id) {
    Runtime& runtime = module.runtime;

    if (std::erase_if(runtime.breakpoints, [&](const Module_Breakpoint& breakpoint) {
            return breakpoint.id == id && breakpoint.owner == &module;
        }) != 0) {
        Push(runtime);
    }
}

void Resume(Module& module, u32 processor, bool step, u64 handler) {
    Runtime& runtime = module.runtime;

    if (step && processor >= runtime.config.processors.size()) {
        return;
    }

    runtime.paused = false;
    runtime.stepOwner = step ? &module : nullptr;
    if (step) {
        runtime.stepHandler = handler;
        runtime.stepProcessor = processor;
    }
    Push(runtime);
}

bool Cpu(Module& module, u32 processor, u64 address, u64 size, bool write) {
    Runtime& runtime = module.runtime;
    void* state = module.Memory(address, size);

    if (runtime.pausedState == nullptr || state == nullptr || size != runtime.pausedStateSize || processor != runtime.pausedProcessor) {
        return false;
    }

    memcpy(write ? runtime.pausedState : state, write ? state : runtime.pausedState, size);
    return true;
}

NativeSymbol sNatives[] = {
    Native("breakpoint_add", "(iIIiI)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Require_Emulation_Thread(exec_env, "Breakpoint::Add")
            ? Add(Module_Of(exec_env), static_cast<u32>(slots[0]), slots[1], slots[2], static_cast<u32>(slots[3]), slots[4])
            : 0;
    }),
    Native("breakpoint_remove", "(i)", [](wasm_exec_env_t exec_env, u64* slots) {
        if (Require_Emulation_Thread(exec_env, "Breakpoint::Remove")) {
            Remove(Module_Of(exec_env), static_cast<u32>(slots[0]));
        }
    }),
    Native("breakpoint_resume", "(iiI)", [](wasm_exec_env_t exec_env, u64* slots) {
        if (Require_Emulation_Thread(exec_env, "Breakpoint::Resume")) {
            Resume(Module_Of(exec_env), static_cast<u32>(slots[0]), slots[1] != 0, slots[2]);
        }
    }),
    Native("breakpoint_paused", "()i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Module_Of(exec_env).runtime.paused ? 1 : 0;
    }),
    Native("breakpoint_cpu", "(iIIi)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Require_Emulation_Thread(exec_env, "Breakpoint::Get_Cpu") && Cpu(Module_Of(exec_env), static_cast<u32>(slots[0]), slots[1], slots[2], slots[3] != 0) ? 1 : 0;
    }),
};

} // namespace

void Breakpoints_Register_Natives() {
    Register_Natives(sNatives, "breakpoint");
}

void Breakpoints_Release_Owner(Module& owner) {
    Runtime& runtime = owner.runtime;
    bool stepping = runtime.stepOwner == &owner;
    usize removed = std::erase_if(runtime.breakpoints, [&](const Module_Breakpoint& breakpoint) {
        return breakpoint.owner == &owner;
    });

    if (stepping) {
        runtime.stepOwner = nullptr;
    }

    if (removed != 0 || stepping) {
        Push(runtime);
    }
}

void Runtime::Breakpoint(const ModLoader_Break& hit, void* state, u64 size) {
    Module* owner = nullptr;
    u64 handler = 0;
    bool pause = false;

    if (hit.processor >= config.processors.size() || size != config.processors[hit.processor].stateSize || (size != 0 && state == nullptr)) {
        return;
    }

    if (hit.id == 0 && stepOwner != nullptr && hit.processor == stepProcessor) {
        owner = stepOwner;
        handler = stepHandler;
        stepOwner = nullptr;
    }

    if (hit.id != 0) {
        for (const Module_Breakpoint& breakpoint : breakpoints) {
            if (breakpoint.id == hit.id && breakpoint.processor == hit.processor) {
                owner = breakpoint.owner;
                handler = breakpoint.handler;
                break;
            }
        }
    }

    if (owner != nullptr) {
        pause = Call_Handler(*owner, handler, hit, state, size);
    }
    
    pausedState = state;
    pausedProcessor = hit.processor;
    pausedStateSize = size;
    paused = pause;
    if (paused) {
        Hold(MODLOADER_PAUSE_BREAKPOINT, [this] {
            return paused;
        });
    }
    paused = false;
    pausedState = nullptr;
    pausedProcessor = UINT32_MAX;
    pausedStateSize = 0;
    Flush_Dirty_Pages();
}
