#include "listener_list.h"

#include <modloader/emulation.h>
#include <modloader/savestate.h>

namespace {

[[clang::no_destroy]] ModLoader::Runtime::Listener_List<ModLoader::Emulation::Event, 3> sListeners({
    MODLOADER_EVENT_PAUSE,
    MODLOADER_EVENT_RESUME,
    MODLOADER_EVENT_RESTART
});

} // namespace

u32 ModLoader::Emulation::Listen(Function<void(const Event& event)> listener) {
    return sListeners.Add(std::move(listener));
}

ModLoader::Subscription ModLoader::Emulation::Subscribe(Function<void(const Event&)> listener) {
    return sListeners.Subscribe(std::move(listener));
}

void ModLoader::Runtime::Listeners_Shutdown() {
    Config_Shutdown();
    Ui_Shutdown();
    sListeners.Close();
}

void ModLoader::Emulation::Unlisten(u32 listener) {
    sListeners.Remove(listener);
}

void ModLoader::Emulation::Pause() {
    ModLoader_Host_Emulation_Pause(1);
}

void ModLoader::Emulation::Resume() {
    ModLoader_Host_Emulation_Pause(0);
}

void ModLoader::Emulation::Restart() {
    ModLoader_Host_Emulation_Restart();
}

bool ModLoader::Emulation::Paused() {
    return ModLoader_Host_Emulation_Paused() != 0;
}

ModLoader::Subscription ModLoader::Savestate::Detail::Block(std::string_view owner, std::string_view reason) {
    u64 token = ModLoader_Host_Savestate_Block(owner.data(), owner.size(), reason.data(), reason.size());
    if (token == 0) {
        return {};
    }

    return Subscription([token] {
        ModLoader_Host_Savestate_Unblock(token);
    });
}

void ModLoader::Runtime::Emulation_Event(u32 event, const void* record, u64 size) {
    ModLoader_Pause_Record pause = {};
    Emulation::Event happened = {};

    if (event == MODLOADER_EVENT_PAUSE && size >= sizeof(pause)) {
        __builtin_memcpy(&pause, record, sizeof(pause));
    }
    
    happened.kind = event == MODLOADER_EVENT_PAUSE ? Emulation::Event_Kind::Paused
        : event == MODLOADER_EVENT_RESUME          ? Emulation::Event_Kind::Resumed
                                                   : Emulation::Event_Kind::Restarting;
    happened.breakpoint = pause.reason == MODLOADER_PAUSE_BREAKPOINT;
    sListeners.Call(happened);
}
