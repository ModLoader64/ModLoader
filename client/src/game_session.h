#pragma once

#include "client_settings.h"
#include "modloader_adapter_api.h"
#include "platform.h"
#include "presenter.h"
#include "runtime.h"
#include "session.h"

class Game_Session final : public Runtime_Host {
public:
    explicit Game_Session(const Run_Options& run_options);
    Game_Session(const Game_Session&) = delete;
    Game_Session& operator=(const Game_Session&) = delete;
    ~Game_Session();

    s32 Run();

    void Invalidate_Code(u32 space, u64 offset, u64 size) override;
    bool Translate_Address(u32 processor, u64 address, u32& out_space, u64& out_offset) override;
    void Request_Quit() override;
    s64 Platform_Query(u32 query, void* output, u64 capacity) override;
    void Set_Breakpoints(std::span<const ModLoader_Breakpoint> breakpoints, u32 step_processor) override;

    bool Has_Breakpoints() const override {
        return adapter->setBreakpoints != nullptr;
    }

    bool Quitting() const override {
        return quitRequested;
    }

    s32 Set_Texture_Sources(std::span<const char* const> paths, u32 flags) override;
    bool Resize_Image(u64 size) override;
    u64 Peek_Memory(u32 processor, u64 address, std::span<u8> buffer) override;
    u64 Poke_Memory(u32 processor, u64 address, std::span<const u8> data) override;
    void Pause_Emulation(bool paused) override;

    bool Emulation_Paused() const override {
        return pauseRequested;
    }

    void Restart_Emulation() override;

private:
    bool Open_Adapter();
    bool Start_Adapter(const std::string& lobby_name);
    bool Start_Runtime(const std::string& nickname, const std::string& lobby_name);
    void Load_Modules();
    void Build_Ui();
    void On_Event(u32 event, u64 time, const void* data, u64 size);
    void Take_Requests();
    void Hold();
    void Restart();
    void Emulation_Main();
    void Bind_Hotkey(const std::string& key, const std::string& value);
    void Handle_Hotkeys(u32 pressed);
    void Toggle_Speed_Limit();
    bool Has_Savestates() const;
    void Load_Adapter_Settings();
    void Describe_Adapter_Settings(bool bind);
    void Apply_Adapter_Settings();
    void Adapter_Setting_Changed(const std::string& key, const std::string& value);
    void Bind_Control(std::string_view key, std::string_view value);

    const Run_Options& options;
    Library library;
    ModLoader_Platform_Adapter adapterStorage = {};
    const ModLoader_Platform_Adapter* adapter = nullptr;
    void* handle = nullptr;
    std::unique_ptr<Platform> platform;
    Game_Image image;
    std::unique_ptr<Runtime> runtime;
    std::unique_ptr<Presenter> presenter;
    Client_Settings clientSettings;
    Settings_Group* adapterSettings = nullptr;
    std::vector<std::pair<std::string, std::string>> pendingSettings;
    std::atomic<bool> settingsPending = false;
    Thread emulationThread;
    Thread loaderThread;
    std::atomic<bool> modulesLoaded = false;
    std::atomic<bool> quitRequested = false;
    std::atomic<bool> emulationDone = false;
    std::atomic<bool> pauseRequested = false;
    std::atomic<bool> restartRequested = false;
    bool loadPending = false;
    s32 runResult = 0;
    u8* imageBase = nullptr;
    u64 imageCapacity = 0;
    std::string saveDirectory;
};
