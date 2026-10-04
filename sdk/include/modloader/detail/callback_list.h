#pragma once

#include <modloader/subscription.h>
#include <modloader/types.h>
#include <algorithm>
#include <vector>

namespace ModLoader::Detail {

template<typename... Arguments>
class Callback_List {
public:
    using Callback = Function<void(Arguments...)>;

    constexpr Callback_List() = default;
    Callback_List(const Callback_List&) = delete;
    Callback_List& operator=(const Callback_List&) = delete;

    ~Callback_List() {
        Close();
    }

    u32 Add(Callback callback) {
        auto slot = Add_Record(std::move(callback));
        return slot != nullptr ? slot->id : 0;
    }

    [[nodiscard]] Subscription Subscribe(Callback callback) {
        auto slot = Add_Record(std::move(callback));
        if (slot == nullptr) {
            return {};
        }

        return slot->Subscribe([weak = std::weak_ptr<Slot>(slot)] {
            if (auto active = weak.lock()) {
                active->Clear();
            }
        });
    }

    Subscription Scope(u32 id, Function<void()> cleanup) {
        auto slot = Find(id);
        return slot != nullptr ? slot->Subscribe(std::move(cleanup)) : Subscription{};
    }

    void Remove(u32 id) {
        if (auto slot = Find(id)) {
            slot->Clear();
        }
    }
    bool Empty() const {
        return listeners == nullptr || std::none_of(listeners->slots.begin(), listeners->slots.end(),
            [](const auto& slot) { return slot->callback != nullptr; });
    }

    // Callers synchronize dispatch and registration across threads
    void Call(Arguments... arguments) {
        const auto active = listeners;
        if (active == nullptr) {
            return;
        }

        const usize count = active->slots.size();
        active->depth++;
        for (usize index = 0; index < count; index++) {
            auto callback = active->slots[index]->callback;
            if (callback != nullptr) {
                (*callback)(arguments...);
            }
        }

        if (--active->depth == 0) {
            Prune(*active);
        }
    }

    void Close() {
        auto retired = std::exchange(listeners, nullptr);
        if (retired == nullptr) {
            return;
        }

        for (const auto& slot : retired->slots) {
            slot->Clear();
        }
    }

private:
    struct Slot {
        u32 id;
        std::shared_ptr<Callback> callback;
        std::weak_ptr<Subscription_State> subscription;

        Subscription Subscribe(Function<void()> cleanup) {
            auto state = std::make_shared<Subscription_State>(std::move(cleanup));
            subscription = state;
            return Subscription(std::move(state));
        }

        void Clear() {
            auto retired = std::exchange(callback, nullptr);
            if (auto state = subscription.lock()) {
                state->Dismiss();
            }
        }
    };

    struct List {
        std::vector<std::shared_ptr<Slot>> slots;
        u32 nextId = 0;
        u32 depth = 0;
    };

    static void Prune(List& list) {
        std::erase_if(list.slots, [](const auto& slot) { return slot->callback == nullptr; });
    }

    std::shared_ptr<Slot> Find(u32 id) const {
        if (listeners != nullptr && id != 0) {
            for (const auto& slot : listeners->slots) {
                if (slot->id == id && slot->callback != nullptr) {
                    return slot;
                }
            }
        }
        return {};
    }
    
    std::shared_ptr<Slot> Add_Record(Callback callback) {
        if (!callback) {
            return {};
        }
        if (listeners == nullptr) {
            listeners = std::make_shared<List>();
        }
        if (listeners->depth == 0) {
            Prune(*listeners);
        }
        if (listeners->nextId == static_cast<u32>(-1)) {
            return {};
        }
        auto slot = std::make_shared<Slot>();
        slot->id = ++listeners->nextId;
        slot->callback = std::make_shared<Callback>(std::move(callback));
        listeners->slots.push_back(slot);
        return slot;
    }

    std::shared_ptr<List> listeners;
};

} // namespace ModLoader::Detail

