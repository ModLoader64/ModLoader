#include "game_session.h"

#include "network/names.h"
#include "settings.h"
#include "ui/toolbar.h"

namespace {

constexpr std::pair<std::string_view, Hotkey> gHotkeys[] = {
    { "hotkeys.pause", Hotkey::Pause },
    { "hotkeys.restart", Hotkey::Restart },
    { "hotkeys.unthrottled", Hotkey::Unthrottled },
};

} // namespace

std::string Aot_Compiler_Path() {
#if defined(_WIN32)
    std::string path = Path_Join(Path_Executable_Directory(), "wamrc.exe");
#else
    std::string path = Path_Join(Path_Executable_Directory(), "wamrc");
#endif

    if (!File_Exists(path)) {
        Log_Error("runtime", "AOT execution requires wamrc");
        return {};
    }
    return path;
}

Game_Session::Game_Session(const Run_Options& run_options) : options(run_options),
clientSettings([this](Settings_Group&, const std::string& key, const std::string& value) {
        Bind_Hotkey(key, value);
        presenter->Set_Picture(clientSettings.Scaling(), clientSettings.Filter());
        presenter->Set_Theme(clientSettings.Theme());
    }) {
}

Game_Session::~Game_Session() {
    if (presenter != nullptr) {
        presenter->Release_Frames();
    }

    if (handle != nullptr) {
        Apply_Adapter_Settings();
        adapter->destroy(handle);
    }

    runtime.reset();
    presenter.reset();
    library.Close();
}

void Game_Session::Request_Quit() {
    if (!quitRequested.exchange(true)) {
        adapter->requestQuit(handle);
    }
}

void Game_Session::Pause_Emulation(bool paused) {
    pauseRequested = paused;
}

void Game_Session::Restart_Emulation() {
    restartRequested = true;
    pauseRequested = false;
}

void Game_Session::Load_Modules() {
    runtime->Load_Modules(options.modulePaths, [this](Runtime_Stage stage, const char* name, u32 index, u32 count) {
        const char* action = "Loading";
        switch (stage) {
        case Runtime_Stage::Preparing:
            action = "Preparing";
            break;
        case Runtime_Stage::Linking:
            action = "Linking";
            break;
        case Runtime_Stage::Compiling:
            action = "Compiling";
            break;
        case Runtime_Stage::Loading:
            break;
        }

        std::string status = Text_Format("%s %s...", action, name);
        if (count > 1 && index != 0) {
            status += Text_Format(" %u / %u", index, count);
        }

        presenter->Set_Status(status);
    });

    modulesLoaded = true;
    presenter->Wake();
}

void Game_Session::Build_Ui() {
    Toolbar_State state = {
        pauseRequested,
        clientSettings.Get("hotkeys.pause"),
        clientSettings.Get("hotkeys.restart"),
        runtime->textures,
        Has_Savestates(),
    };

    switch (Toolbar_Build(state)) {
    case Toolbar_Action_Pause:
        Pause_Emulation(!pauseRequested);
        break;
    case Toolbar_Action_Restart:
        Restart_Emulation();
        break;
    case Toolbar_Action_Save_State:
        presenter->Post_Request(Presenter_Request::Save_State);
        break;
    case Toolbar_Action_Load_State:
        presenter->Post_Request(Presenter_Request::Load_State);
        break;
    case Toolbar_Action_Quit:
        Request_Quit();
        break;
    default:
        break;
    }
    runtime->Ui();
}

void Game_Session::Bind_Hotkey(const std::string& key, const std::string& value) {
    for (const auto& [name, hotkey] : gHotkeys) {
        if (key == name) {
            presenter->Bind_Hotkey(hotkey, value);
        }
    }
}

void Game_Session::Handle_Hotkeys(u32 pressed) {
    if ((pressed & (1u << static_cast<u32>(Hotkey::Pause))) != 0) {
        Pause_Emulation(!pauseRequested);
    }

    if ((pressed & (1u << static_cast<u32>(Hotkey::Restart))) != 0) {
        Restart_Emulation();
    }

    if ((pressed & (1u << static_cast<u32>(Hotkey::Unthrottled))) != 0) {
        Toggle_Speed_Limit();
    }
}

void Game_Session::Toggle_Speed_Limit() {
    std::string key;
    bool limited = false;

    {
        std::unique_lock lock = Settings_Lock();
        for (const Setting& setting : adapterSettings->Entries()) {
            if ((setting.flags & MODLOADER_SETTING_SPEED_LIMIT) != 0) {
                key = setting.key;
                limited = setting.value == "true";
            }
        }
    }

    if (key.empty()) {
        Log_Warning("session", "adapter does not support speed limiting");
        return;
    }

    adapterSettings->Set(key, limited ? "false" : "true");
}

void Game_Session::On_Event(u32 event, u64 time, const void* data, u64 size) {
    runtime->Event(event, time, data, size);
    if (event != MODLOADER_EVENT_REFRESH) {
        return;
    }

    loadPending = false;
    Take_Requests();

    if (restartRequested.exchange(false)) {
        Restart();
    }
    else if (pauseRequested && !quitRequested && !loadPending) {
        Hold();
    }
}

void Game_Session::Take_Requests() {
    Presenter_Request request;

    Apply_Adapter_Settings();
    request = presenter->Take_Request();
    if (!Has_Savestates()) {
        return;
    }

    if (request == Presenter_Request::Save_State) {
        platform->Save_State(*adapter, handle);
    }
    else if (request == Presenter_Request::Load_State) {
        loadPending = platform->Load_State(*adapter, handle);
    }
    else {
        platform->Retry_Save_State(*adapter, handle);
    }
}

bool Game_Session::Has_Savestates() const {
    return platform->savestates && adapter->saveState != nullptr && adapter->loadState != nullptr;
}

void Game_Session::Hold() {
    loadPending = false;
    runtime->Hold(MODLOADER_PAUSE_ASKED, [this] {
        Take_Requests();
        return pauseRequested && !restartRequested && !loadPending;
    });

    if (restartRequested.exchange(false)) {
        Restart();
    }
}

void Game_Session::Restart() {
    runtime->Notify(MODLOADER_EVENT_RESTART);
    if (adapter->reset == nullptr || adapter->reset(handle) != 0) {
        Log_Warning("session", "adapter cannot restart the game");
        return;
    }
}

void Game_Session::Emulation_Main() {
    runtime->Enter_Thread();
    runtime->Init_Modules();
    runResult = adapter->run(handle);
    runtime->Shutdown_Modules();
    runtime->Leave_Thread();
    emulationDone = true;
    presenter->Wake();
}

s32 Game_Session::Run() {
    Online_Names names = Random_Names();
    Presenter::Config presenter_config;

    std::string nickname = !options.nickname.empty() ? options.nickname : clientSettings.Get("network.nickname");
    std::string lobby_name = !options.lobby.empty() ? options.lobby : clientSettings.Get("network.lobby");
    if (nickname.empty()) {
        nickname = names.nickname;
    }

    if (lobby_name.empty()) {
        lobby_name = names.lobby;
    }

    if (!Open_Adapter() || !platform->Load_Image(options.romPath, image)) {
        return 1;
    }

    presenter_config.title = "ModLoader - " + image.title + (options.Online() || !clientSettings.Get("network.server_address").empty() ? " - " + nickname : "");
    presenter_config.scaling = clientSettings.Scaling();
    presenter_config.filter = clientSettings.Filter();
    presenter_config.theme = clientSettings.Theme();
    presenter_config.dataDirectory = options.dataDirectory;
    presenter = Presenter::Create(presenter_config);
    if (presenter == nullptr) {
        return 1;
    }

    for (const auto& [name, hotkey] : gHotkeys) {
        presenter->Bind_Hotkey(hotkey, clientSettings.Get(name));
    }

    if (!Start_Adapter(lobby_name) || !Start_Runtime(nickname, lobby_name)) {
        return 1;
    }

    if (!loaderThread.Start([this] {
            Load_Modules();
        })) {
        Load_Modules();
    }

    while (!modulesLoaded) {
        if (!presenter->Pump()) {
            quitRequested = true;
        }
    }

    loaderThread.Join();
    presenter->Set_Status("");
    if (quitRequested) {
        return 1;
    }

    presenter->Set_Ui([this] {
        Build_Ui();
    });

    if (!emulationThread.Start([this] { Emulation_Main(); })) {
        Log_Error("session", "cannot start the emulation thread");
        return 1;
    }

    while (!emulationDone) {
        if (!presenter->Pump()) {
            Request_Quit();
        }
        Handle_Hotkeys(presenter->Take_Hotkeys());
    }
    
    emulationThread.Join();
    return runResult == 0 ? 0 : 1;
}

s32 Session_Run(const Run_Options& options) {
    s32 exit_code;

    Settings_Start(options.dataDirectory);
    {
        Game_Session session(options);

        exit_code = session.Run();
    }
    Settings_Stop();
    return exit_code;
}
