#pragma once

#include <modloader/function.h>
#include <modloader/async/promise.h>

#include <type_traits>
#include <utility>

namespace ModLoader::Task {

namespace Detail {
bool Is_Cancelled(u32 promise);
} // namespace Detail

// copies keep the task's completion state alive
class Cancellation_Token {
public:
    explicit Cancellation_Token(u32 promise)
        : reference(promise) {
        Promise_Core::Retain(promise);
    }

    bool Is_Cancelled() const {
        return Detail::Is_Cancelled(reference.Handle());
    }

private:
    Promise_Core::Reference reference;
};

// queue work for an existing promise
bool Submit(u32 promise, Function<void()> work);

namespace Detail {

template<typename Callable>
decltype(auto) Invoke(Callable& work, const Cancellation_Token& cancellation) {
    if constexpr (std::is_invocable_v<Callable&, const Cancellation_Token&>) {
        return work(cancellation);
    }
    else {
        return work();
    }
}

} // namespace Detail

// Run work on a host worker and resolve its return value (or void) on completion
// Work may accept const Cancellation_Token&; poll it to stop early; cancellation does not interrupt running work
template<typename Callable>
auto Run(Callable work) {
    using Result = decltype(Detail::Invoke(work, std::declval<const Cancellation_Token&>()));
    Promise<Result> promise = Promise<Result>::Create();
    Submit(promise.Handle(), [promise, work = std::move(work)]() mutable {
        Cancellation_Token cancellation(promise.Handle());
        if constexpr (std::is_void_v<Result>) {
            Detail::Invoke(work, cancellation);
            promise.Resolve();
        }
        else {
            promise.Resolve(Detail::Invoke(work, cancellation));
        }
    });
    return promise;
}

} // namespace ModLoader::Task
