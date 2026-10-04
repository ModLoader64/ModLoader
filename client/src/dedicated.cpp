#include "session.h"

#include "client_settings.h"
#include "modloader_events.h"
#include "modloader_platform.h"
#include "runtime.h"
#include "settings.h"

#include <signal.h>

namespace {

constexpr u32 gTickMilliseconds = 16;

volatile sig_atomic_t sStop;

class Server_Host final : public Runtime_Host {
public:
    void Request_Quit() override {
        sStop = 1;
    }
};

s32 Serve(const Run_Options& options) {
    Server_Host host;
    Platform platform;
    std::unique_ptr<Runtime> runtime;
    Runtime_Config config;
    Client_Settings client_settings;
    u64 start;

    config.platform = &platform;
    config.dataDirectory = options.dataDirectory;
    config.moduleExecution = client_settings.Execution_Mode();
    if (config.moduleExecution != Module_Execution::Interpreter) {
        config.aotCompiler = Aot_Compiler_Path();
    }
    config.session = MODLOADER_SESSION_HOSTING;
    config.serverConfig = { options.hostPort, options.hostAddress, options.dataDirectory };
    config.host = &host;
    runtime = Runtime::Create(std::move(config));
    if (runtime == nullptr) {
        return 1;
    }
    runtime->Load_Modules(options.modulePaths, {});
    signal(SIGINT, [](int) {
        sStop = 1;
    });
    signal(SIGTERM, [](int) {
        sStop = 1;
    });
    runtime->Enter_Thread();
    runtime->Init_Modules();
    if (runtime->Server() == nullptr) {
        runtime->Shutdown_Modules();
        runtime->Leave_Thread();
        return 1;
    }

    start = Time_Monotonic_Microseconds();
    while (sStop == 0) {
        Time_Sleep_Milliseconds(gTickMilliseconds);
        runtime->Event(MODLOADER_EVENT_REFRESH, (Time_Monotonic_Microseconds() - start) * 1000, nullptr, 0);
    }
    
    runtime->Shutdown_Modules();
    runtime->Leave_Thread();
    return 0;
}

} // namespace

s32 Dedicated_Run(const Run_Options& options) {
    s32 exit_code;

    Settings_Start(options.dataDirectory);
    exit_code = Serve(options);
    Settings_Stop();
    return exit_code;
}
