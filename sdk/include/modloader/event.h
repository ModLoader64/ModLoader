#pragma once

#include <modloader/detail/callback_list.h>

namespace ModLoader {

// Ordered synchronous callbacks. Keep each Subscribe() result alive to receive events
// Safe to subscribe/unsubscribe or destroy the event from a callback
template<typename... Arguments>
class Event {
public:
    using Callback = Function<void(Arguments...)>;

    constexpr Event() = default;
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

    [[nodiscard]] Subscription Subscribe(Callback callback) {
        return listeners.Subscribe(std::move(callback));
    }

    // Calls listeners
    void Emit(Arguments... arguments) {
        listeners.Call(arguments...);
    }

    // Removes all listeners; the event can be reused
    void Close() {
        listeners.Close();
    }

private:
    Detail::Callback_List<Arguments...> listeners;
};

} // namespace ModLoader
