#pragma once

#include <modloader/types.h>
#include <modloader_events.h>

#include <span>
#include <cstdlib>

namespace ModLoader::Runtime {

// Generated component registration; plugin code uses lifecycle.h
template<typename Signature>
struct Event_Callbacks;

template<typename... Arguments>
struct Event_Callbacks<void(Arguments...)> {
    using Callback = void (*)(Arguments...);

    Callback pre = nullptr;
    Callback normal = nullptr;
    Callback post = nullptr;

    bool Any() const {
        return pre != nullptr || normal != nullptr || post != nullptr;
    }

    void Call(ModLoader_Event_Phase phase, Arguments... arguments) const {
        Callback callback = phase == MODLOADER_EVENT_PHASE_PRE ? pre : phase == MODLOADER_EVENT_PHASE_NORMAL ? normal : post;
        if (callback != nullptr) {
            callback(arguments...);
        }
    }

    void Call_All(Arguments... arguments) const {
        Call(MODLOADER_EVENT_PHASE_PRE, arguments...);
        Call(MODLOADER_EVENT_PHASE_NORMAL, arguments...);
        Call(MODLOADER_EVENT_PHASE_POST, arguments...);
    }
};

struct Component {
    void (*prepare)() = nullptr;
#define MODLOADER_LIFECYCLE(field, arguments, pre, normal, post, event) Event_Callbacks<void arguments> field;
#include <modloader/detail/lifecycle.def>
#undef MODLOADER_LIFECYCLE
    const void* platform = nullptr;
};

void Register_Component(const Component* component);
void Prepare_Components();
std::span<const Component* const> Components();

template<typename Signature>
struct Component_Callbacks;

template<typename... Arguments>
struct Component_Callbacks<void(Arguments...)> {
    using Callbacks = Event_Callbacks<void(Arguments...)>;
    const Callbacks** listeners = nullptr;
    usize count = 0;

    template<typename Select>
    void Prepare(Select select) {
        for (const Component* component : Components()) {
            const Callbacks* callbacks = select(component);
            if (callbacks != nullptr && callbacks->Any()) {
                count++;
            }
        }

        if (count != 0) {
            listeners = static_cast<const Callbacks**>(malloc(count * sizeof(*listeners)));
            if (listeners == nullptr) {
                abort();
            }

            usize index = 0;
            for (const Component* component : Components()) {
                const Callbacks* callbacks = select(component);
                if (callbacks != nullptr && callbacks->Any()) {
                    listeners[index++] = callbacks;
                }
            }
        }
    }

    bool Any() const {
        return count != 0;
    }

    void Call(ModLoader_Event_Phase phase, Arguments... arguments) const {
        for (usize index = 0; index < count; index++) {
            listeners[phase == MODLOADER_EVENT_PHASE_POST ? count - index - 1 : index]->Call(phase, arguments...);
        }
    }

    void Call_All(bool reverse = false) const requires (sizeof...(Arguments) == 0) {
        for (usize index = 0; index < count; index++) {
            listeners[reverse ? count - index - 1 : index]->Call_All();
        }
    }
};

struct Component_Events {
#define MODLOADER_LIFECYCLE(field, arguments, pre, normal, post, event) Component_Callbacks<void arguments> field;
#include <modloader/detail/lifecycle.def>
#undef MODLOADER_LIFECYCLE
};

extern Component_Events gComponentEvents;

} // namespace ModLoader::Runtime
