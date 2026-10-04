#pragma once

#include <modloader/async/promise.h>

#include <coroutine>

namespace ModLoader {

template <typename T>
struct Result;

namespace Coroutine_Core {

void Suspend_On(u32 awaited, void* frame, u32 owner, bool propagate);

struct Frame_Base {};

} // namespace Coroutine_Core

template <typename T>
class Coroutine;

namespace Coroutine_Core {

template <typename T>
struct Promise_Type_Base : Frame_Base {
    Promise<T> result = Promise<T>::Create();

    Coroutine<T> get_return_object();
    static Coroutine<T> get_return_object_on_allocation_failure();

    std::suspend_never initial_suspend() noexcept {
        return {};
    }

    std::suspend_never final_suspend() noexcept {
        return {};
    }

    void unhandled_exception() noexcept {
        __builtin_trap();
    }
};

template <typename T>
struct Promise_Type : Promise_Type_Base<T> {
    void return_value(T value) {
        this->result.Resolve(std::move(value));
    }
};

template <>
struct Promise_Type<void> : Promise_Type_Base<void> {
    void return_void() {
        this->result.Resolve();
    }
};

template <typename T, bool gPropagate>
struct Awaiter {
    Promise<T> awaited;

    bool await_ready() const {
        return gPropagate ? awaited.Is_Fulfilled() : !awaited.Is_Pending();
    }

    template <typename Frame_Promise>
    bool await_suspend(std::coroutine_handle<Frame_Promise> coroutine) {
        static_assert(std::is_base_of_v<Frame_Base, Frame_Promise>, "co_await on a promise works in ModLoader coroutines");
        Suspend_On(awaited.Handle(), coroutine.address(), coroutine.promise().result.Handle(), gPropagate);
        return true;
    }

    auto await_resume() const {
        if constexpr (gPropagate) {
            if constexpr (!std::is_void_v<T>) {
                return awaited.Value();
            }
        }
        else if constexpr (std::is_void_v<T>) {
            return Result<T>{ awaited.Get_Error() };
        }
        else {
            return Result<T>{ awaited.Get_Error(), awaited.Value() };
        }
    }
};

} // namespace Coroutine_Core

} // namespace ModLoader

