#pragma once

#include "internal.h"
#include <modloader/detail/callback_list.h>
#include <array>

namespace ModLoader::Runtime {

template<typename Event, usize EventCount = 1>
class Listener_List {
public:
    using Listener = Function<void(const Event&)>;

    explicit Listener_List(const std::array<u32, EventCount>& events) : events(events) {
    }

    u32 Add(Listener listener) {
        if (closed || !listener) {
            return 0;
        }

        if (!following && !Follow()) {
            return 0;
        }
        
        const u32 id = listeners.Add(std::move(listener));
        if (listeners.Empty()) {
            Unfollow();
        }
        return id;
    }

    Subscription Scope(u32 id, Function<void()> cleanup) {
        return listeners.Scope(id, std::move(cleanup));
    }

    Subscription Subscribe(Listener listener) {
        const u32 id = Add(std::move(listener));
        return Scope(id, [this, id] { Remove(id); });
    }

    void Close() {
        closed = true;
        listeners.Close();
        Unfollow();
    }

    void Remove(u32 listener) {
        listeners.Remove(listener);
        if (listeners.Empty()) {
            Unfollow();
        }
    }

    bool Empty() const {
        return listeners.Empty();
    }

    void Call(const Event& happened) {
        listeners.Call(happened);
    }

private:
    bool Follow() {
        for (usize index = 0; index < events.size(); ++index) {
            if (!Follow_Event(events[index], true)) {
                while (index != 0) {
                    Follow_Event(events[--index], false);
                }
                return false;
            }
        }
        following = true;
        return true;
    }

    void Unfollow() {
        if (std::exchange(following, false)) {
            for (u32 event : events) {
                Follow_Event(event, false);
            }
        }
    }

    std::array<u32, EventCount> events;
    bool following = false;
    bool closed = false;
    Detail::Callback_List<const Event&> listeners;
};

} // namespace ModLoader::Runtime
