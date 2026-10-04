#pragma once

#include <modloader/function.h>

#include <memory>
#include <utility>

namespace ModLoader {

namespace Detail {

struct Subscription_State {
    Function<void()> cleanup;

    void Reset() {
        if (auto release = std::exchange(cleanup, {})) {
            release();
        }
    }

    void Dismiss() {
        cleanup = {};
    }
};

} // namespace Detail

class Subscription {
public:
    Subscription() = default;

    explicit Subscription(Function<void()> cleanup) {
        if (cleanup) {
            state = std::make_shared<Detail::Subscription_State>(std::move(cleanup));
        }
    }

    explicit Subscription(std::shared_ptr<Detail::Subscription_State> state) : state(std::move(state)) {
    }

    ~Subscription() {
        Reset();
    }

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&&) noexcept = default;

    Subscription& operator=(Subscription&& other) noexcept {
        if (this != &other) {
            Reset();
            state = std::move(other.state);
        }
        return *this;
    }

    void Reset() {
        if (auto retired = std::exchange(state, nullptr)) {
            retired->Reset();
        }
    }

    explicit operator bool() const {
        return state != nullptr && static_cast<bool>(state->cleanup);
    }

private:
    std::shared_ptr<Detail::Subscription_State> state;
};

} // namespace ModLoader
