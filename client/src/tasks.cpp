#include "module.h"

#include <algorithm>
#include <condition_variable>
#include <deque>

namespace {

struct Task_Context {
    Module_Context wasm;
    wasm_function_inst_t run = nullptr;
    bool busy = false;
};

struct Task_Owner {
    Module* module;
    std::vector<std::unique_ptr<Task_Context>> contexts;
    std::deque<u64> jobs;
    usize running = 0;
    bool closing = false;
    bool ready = false;
};

struct Task_Pool {
    std::mutex lock;
    std::condition_variable changed;
    std::unordered_map<Module*, std::unique_ptr<Task_Owner>> owners;
    std::deque<Task_Owner*> ready;
    std::vector<std::unique_ptr<Thread>> workers;
    bool stopping = false;

    ~Task_Pool() {
        {
            std::lock_guard guard(lock);
            stopping = true;
        }
        changed.notify_all();
        for (const auto& worker : workers) {
            worker->Join();
        }
    }
};

Task_Pool sPool;
thread_local bool sTaskWorker = false;

Task_Context* Available_Context(Task_Owner& owner) {
    for (const auto& context : owner.contexts) {
        if (!context->busy) {
            return context.get();
        }
    }
    return nullptr;
}

void Schedule(Task_Owner& owner) {
    if (!owner.ready && !owner.jobs.empty() && Available_Context(owner) != nullptr) {
        owner.ready = true;
        sPool.ready.push_back(&owner);
        sPool.changed.notify_one();
    }
}

bool Run(Module& module, wasm_exec_env_t exec_env, wasm_function_inst_t function, u64 context) {
    wasm_val_t argument = Wasm_I64(context);

    if (module.disabled) {
        return false;
    }

    if (module.Execute(exec_env, function, 0, nullptr, 1, &argument)) {
        return true;
    }

    module.Trap(exec_env, "task");
    return false;
}

void Worker_Main() {
    bool initialized = wasm_runtime_init_thread_env();

    sTaskWorker = true;
    for (;;) {
        std::unique_lock guard(sPool.lock);
        sPool.changed.wait(guard, [] { return sPool.stopping || !sPool.ready.empty(); });
        if (sPool.stopping) {
            break;
        }

        Task_Owner& owner = *sPool.ready.front();
        sPool.ready.pop_front();
        owner.ready = false;
        Task_Context& context = *Available_Context(owner);
        u64 job = owner.jobs.front();
        owner.jobs.pop_front();
        context.busy = true;
        owner.running++;
        Schedule(owner);
        guard.unlock();

        if (initialized) {
            Run(*owner.module, context.wasm.execEnv, context.run, job);
        }
        else {
            Log_Error(owner.module->name.c_str(), "cannot initialize task worker");
            owner.module->disabled = true;
        }

        guard.lock();
        context.busy = false;
        if (owner.jobs.empty() && owner.contexts.size() > 1) {
            auto found = std::find_if(owner.contexts.begin(), owner.contexts.end(),
                [&](const auto& candidate) { return candidate.get() == &context; });
            std::unique_ptr<Task_Context> retired = std::move(*found);
            owner.contexts.erase(found);
            guard.unlock();
            owner.module->Destroy_Context(retired->wasm);
            guard.lock();
        }
        owner.running--;
        Schedule(owner);
        sPool.changed.notify_all();
    }

    sTaskWorker = false;
    if (initialized) {
        wasm_runtime_destroy_thread_env();
    }
}

u32 Submit(wasm_exec_env_t exec_env, u64 job) {
    Module& module = Module_Of(exec_env);
    wasm_function_inst_t run = wasm_runtime_lookup_function(wasm_runtime_get_module_inst(exec_env), "modloader_task_run");

    if (module.disabled || module.stopping || run == nullptr) {
        return 0;
    }

    if (sTaskWorker) {
        Run(module, exec_env, run, job);
        return 1;
    }

    std::lock_guard guard(sPool.lock);
    if (sPool.stopping || module.stopping || module.disabled) {
        return 0;
    }

    if (sPool.workers.empty()) {
        u32 worker_count = Thread_Cpu_Count();
        for (u32 index = 0; index < worker_count; index++) {
            auto worker = std::make_unique<Thread>();
            if (!worker->Start(Worker_Main)) {
                break;
            }
            sPool.workers.push_back(std::move(worker));
        }

        if (sPool.workers.empty()) {
            return 0;
        }
    }
    auto& entry = sPool.owners[&module];
    if (entry == nullptr) {
        entry = std::make_unique<Task_Owner>();
        entry->module = &module;
    }

    Task_Owner& owner = *entry;
    if (owner.closing) {
        return 0;
    }

    if (owner.contexts.size() < std::min(owner.jobs.size() + owner.running + 1, sPool.workers.size())) {
        auto context = std::make_unique<Task_Context>();
        if (!module.Create_Context(exec_env, context->wasm)) {
            return 0;
        }
        context->run = wasm_runtime_lookup_function(wasm_runtime_get_module_inst(context->wasm.execEnv), "modloader_task_run");
        owner.contexts.push_back(std::move(context));
    }

    owner.jobs.push_back(job);
    Schedule(owner);
    return 1;
}

void Drain(Module& module) {
    std::unique_lock guard(sPool.lock);
    auto found = sPool.owners.find(&module);

    if (found == sPool.owners.end()) {
        return;
    }

    Task_Owner& owner = *found->second;
    owner.closing = true;
    sPool.changed.wait(guard, [&] { return owner.jobs.empty() && owner.running == 0; });
}

NativeSymbol sNatives[] = {
    Native("task_submit", "(I)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Submit(exec_env, slots[0]);
    }),
    Native("task_drain", "()", [](wasm_exec_env_t exec_env, u64*) {
        if (Require_Emulation_Thread(exec_env, "task_drain")) {
            Drain(Module_Of(exec_env));
        }
    }),
    Native("task_cancelled", "()i", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        slots[0] = module.stopping || module.disabled ? 1 : 0;
    }),
};

} // namespace

void Tasks_Register_Natives() {
    Register_Natives(sNatives, "task");
}

void Tasks_Release_Owner(Module& module) {
    module.stopping = true;
    Drain(module);
    std::unique_ptr<Task_Owner> owner;
    {
        std::lock_guard guard(sPool.lock);
        auto found = sPool.owners.find(&module);
        if (found == sPool.owners.end()) {
            return;
        }
        owner = std::move(found->second);
        sPool.owners.erase(found);
    }
    
    for (const auto& context : owner->contexts) {
        module.Destroy_Context(context->wasm);
    }
}
