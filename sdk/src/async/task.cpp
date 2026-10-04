#include "../internal.h"
#include <modloader/async/task.h>

#include <memory>

extern "C" {
void ModLoader_Task_Run(u64 context) asm("modloader_task_run");
}

namespace {

struct Job {
    ModLoader::Promise_Core::Reference promise;
    ModLoader::Function<void()> work;
};

thread_local u32 sTaskDepth;

} // namespace

bool ModLoader::Task::Detail::Is_Cancelled(u32 promise) {
    return ModLoader_Host_Task_Cancelled() != 0 || Promise_Core::Get_State(promise) != Promise_Core::State::Pending;
}

bool ModLoader::Task::Submit(u32 promise, Function<void()> work) {
    if (!work || Promise_Core::Get_State(promise) != Promise_Core::State::Pending) {
        Promise_Core::Reject(promise, Error::Failed);
        return false;
    }

    Promise_Core::Retain(promise);
    Promise_Core::Reference reference(promise);
    std::unique_ptr<Job> job(new (std::nothrow) Job{std::move(reference), std::move(work)});
    if (!job || ModLoader_Host_Task_Submit(reinterpret_cast<u64>(job.get())) == 0) {
        Promise_Core::Reject(promise, Error::Failed);
        return false;
    }

    job.release();
    return true;
}

void ModLoader::Runtime::Stop_Tasks() {
    ModLoader_Host_Task_Drain();
}

extern "C" MODLOADER_EXPORT("modloader_task_run") void ModLoader_Task_Run(u64 context) {
    std::unique_ptr<Job> job(reinterpret_cast<Job*>(context));
    ++sTaskDepth;
    if (ModLoader::Promise_Core::Get_State(job->promise.Handle()) == ModLoader::Promise_Core::State::Pending) {
        job->work();
    }
    
    job.reset();
    if (--sTaskDepth == 0) {
        ModLoader::Runtime::Scratch_Reset();
    }
}

