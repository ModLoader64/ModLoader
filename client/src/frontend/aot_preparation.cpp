#include "frontend/aot_preparation.h"
#include "frontend/window.h"
#include "module_preparation.h"
#include "session.h"

#include <algorithm>

namespace {

constexpr auto gPreparationDebounceDelay = std::chrono::milliseconds(300);

}

Frontend_Aot_Preparation::~Frontend_Aot_Preparation() {
    Stop();
    worker.Join();
}

void Frontend_Aot_Preparation::Request(std::span<const std::string> selected_paths, const std::string& data_directory, Module_Execution execution_mode) {
    std::lock_guard guard(lock);
    if (mode == execution_mode && dataDirectory == data_directory &&
        std::equal(paths.begin(), paths.end(), selected_paths.begin(), selected_paths.end())) {
        return;
    }

    paths.assign(selected_paths.begin(), selected_paths.end());
    dataDirectory = data_directory;
    mode = execution_mode;
    revision++;
    pending = mode != Module_Execution::Interpreter && !paths.empty();
    deadline = std::chrono::steady_clock::now() + gPreparationDebounceDelay;
    status = pending ? "Preparing enabled mods..." : "";
    if (pending && !started) {
        started = worker.Start([this]() {
            Run();
        });
        if (!started) {
            pending = false;
            status = "Cannot start mod preparation";
        }
    }
    changed.notify_one();
}

bool Frontend_Aot_Preparation::Poll(std::string& out_status) {
    std::lock_guard guard(lock);
    if (out_status == status) {
        return false;
    }
    out_status = status;
    return true;
}

bool Frontend_Aot_Preparation::Stop() {
    std::lock_guard guard(lock);
    stopping = true;
    changed.notify_one();
    return !active;
}

void Frontend_Aot_Preparation::Run() {
    std::unique_lock guard(lock);
    while (!stopping) {
        changed.wait(guard, [this]() {
            return stopping || pending;
        });

        u64 requested_revision = revision;
        if (changed.wait_until(guard, deadline, [this, requested_revision]() {
            return stopping || revision != requested_revision;
        })) {
            continue;
        }

        std::vector<std::string> selected_paths = paths;
        Runtime_Config config;
        config.dataDirectory = dataDirectory;
        config.moduleExecution = mode;
        pending = false;
        active = true;
        guard.unlock();

        config.aotCompiler = Aot_Compiler_Path();
        Runtime_Progress progress = [this, requested_revision](Runtime_Stage stage, const char* name, u32, u32) {
            std::lock_guard progress_guard(lock);
            if (revision == requested_revision && !stopping) {
                const char* action = stage == Runtime_Stage::Compiling ? "Compiling" : stage == Runtime_Stage::Linking ? "Linking" : "Preparing";
                status = Text_Format("%s %s...", action, name);
                Frontend_Wake();
            }
        };
        Module_Preparation preparation{config, &progress};
        auto linked = Link_Modules(preparation, selected_paths);
        bool success = linked && Aot_Compile_All(preparation, *linked);

        guard.lock();
        active = false;
        if (revision == requested_revision && !stopping) {
            status = success ? "Mod preparation finished" : "Mod preparation failed; see log";
        }
        Frontend_Wake();
    }
}
