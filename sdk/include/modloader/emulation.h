#pragma once

#include <functional>
#include <modloader/subscription.h>
#include <modloader/types.h>

namespace ModLoader::Emulation {

enum class Event_Kind : u32 {
    Paused, // Menu, hotkey, plugin, or breakpoint pause
    Resumed,
    Restarting, // Restart requested; On_Reset follows when the game resets
};

struct Event {
    Event_Kind kind;
    bool breakpoint; // Breakpoint pause; resume through Guest::Breakpoint
};

// Notifications run on the main thread; the event is borrowed for the callback
// Resetting or destroying the subscription removes the listener
[[nodiscard]] Subscription Subscribe(std::function<void(const Event&)> listener);

// Pause/restart requests take effect at the next game refresh
void Pause();
void Resume();
void Restart();
bool Paused(); // Requested pause state; breakpoint pauses are tracked separately

} // namespace ModLoader::Emulation
