#include "module.h"

#include <algorithm>

namespace {

void* Thread_Main(wasm_exec_env_t exec_env, void* argument) {
    Module_Thread& thread = *static_cast<Module_Thread*>(argument);
    Module& module = *thread.module;
    wasm_module_inst_t instance = wasm_runtime_get_module_inst(exec_env);
    wasm_function_inst_t start = wasm_runtime_lookup_function(instance, "modloader_thread_start");
    wasm_val_t context = Wasm_I64(thread.context);

    if (start == nullptr) {
        return nullptr;
    }

    if (!module.Adopt_Instance(instance)) {
        module.disabled = true;
        return nullptr;
    }

    if (!module.Execute(exec_env, start, 0, nullptr, 1, &context)) {
        module.Trap(exec_env, "thread");
    }

    {
        std::lock_guard guard(module.resourceLock);
        module.layoutBindings.erase(instance);
    }

    return nullptr;
}

u32 Spawn(Module& module, wasm_exec_env_t exec_env, u64 context) {
    Runtime& runtime = module.runtime;
    std::lock_guard lock(runtime.threadLock);

    if (module.stopping || runtime.nextThreadId == UINT32_MAX) {
        return 0;
    }

    auto thread = std::make_unique<Module_Thread>();
    thread->module = &module;
    thread->context = context;
    if (wasm_runtime_spawn_thread(exec_env, &thread->tid, Thread_Main, thread.get()) != 0) {
        Log_Error(module.name.c_str(), "cannot start thread");
        return 0;
    }

    u32 id = ++runtime.nextThreadId;
    runtime.threads.emplace(id, std::move(thread));
    return id;
}

void Join(Module& module, u32 id) {
    Runtime& runtime = module.runtime;
    std::unique_ptr<Module_Thread> thread;
    {
        std::lock_guard lock(runtime.threadLock);
        auto found = runtime.threads.find(id);
        if (found == runtime.threads.end() || found->second->module != &module) {
            return;
        }
        thread = std::move(found->second);
        runtime.threads.erase(found);
    }
    wasm_runtime_join_thread(thread->tid, nullptr);
}

NativeSymbol sNatives[] = {
    Native("thread_spawn", "(I)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Spawn(Module_Of(exec_env), exec_env, slots[0]);
    }),
    Native("thread_join", "(i)", [](wasm_exec_env_t exec_env, u64* slots) {
        Join(Module_Of(exec_env), static_cast<u32>(slots[0]));
    }),
    Native("thread_sleep", "(i)", [](wasm_exec_env_t, u64* slots) {
        Time_Sleep_Milliseconds(static_cast<u32>(slots[0]));
    }),
    Native("thread_is_emulation", "()i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Module_Of(exec_env).runtime.On_Emulation_Thread() ? 1 : 0;
    }),
    Native("time_milliseconds", "()I", [](wasm_exec_env_t, u64* slots) {
        slots[0] = Time_Monotonic_Milliseconds();
    }),
    // Microseconds: clock 0 is monotonic, 1 is Unix time.
    Native("time_now", "(i)I", [](wasm_exec_env_t, u64* slots) {
        slots[0] = static_cast<u32>(slots[0]) == 1 ? Time_Wall_Microseconds() : Time_Monotonic_Microseconds();
    }),
    Native("cpu_count", "()i", [](wasm_exec_env_t, u64* slots) {
        slots[0] = Thread_Cpu_Count();
    }),
};

} // namespace

void Threads_Register_Natives() {
    Register_Natives(sNatives, "thread");
}

void Threads_Release_Owner(Module& owner) {
    Runtime& runtime = owner.runtime;
    bool terminated = false;

    owner.stopping = true;
    for (;;) {
        std::unique_ptr<Module_Thread> thread;
        {
            std::lock_guard lock(runtime.threadLock);
            auto found = std::find_if(runtime.threads.begin(), runtime.threads.end(),
                [&](const auto& entry) {
                    return entry.second->module == &owner;
                });
            if (found == runtime.threads.end()) {
                break;
            }

            thread = std::move(found->second);
            runtime.threads.erase(found);
        }

        if (!terminated) {
            wasm_runtime_terminate(owner.instance);
            terminated = true;
        }
        
        wasm_runtime_join_thread(thread->tid, nullptr);
    }
}
