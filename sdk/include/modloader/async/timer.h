#pragma once

#include <modloader/function.h>
#include <modloader/subscription.h>
#include <modloader/async/promise.h>

namespace ModLoader::Timer {

using Handle = u32;

// Call on the main thread. Callbacks run with emulation stopped; count is in ticks of the selected clock
// Repeating timers fire once per clock update, skipping missed intervals. A zero interval repeats every tick
Handle Timeout(Clock clock, u64 count, Function<void()> callback);  // Run once, returns 0 on failure.
Handle Interval(Clock clock, u64 count, Function<void()> callback); // Repeat until cancelled, returns 0 on failure

// Scoped equivalents: keep the subscription alive; destroying or resetting it cancels the timer
[[nodiscard]] Subscription Schedule(Clock clock, u64 count, Function<void()> callback);
[[nodiscard]] Subscription Repeat(Clock clock, u64 count, Function<void()> callback);

void Cancel(Handle timer); // Safe for expired or cancelled IDs 
Promise<void> Delay(Clock clock, u64 count); // Resolves after count ticks; cancelling the promise cancels its timer

u64 Now(Clock clock); // Current tick count for this clock

// Custom clocks start at zero and live until module shutdown
// Tick() dispatches their due callbacks immediately
constexpr Clock Invalid_Clock = static_cast<Clock>(~u64{0});
Clock Create_Clock();
void Tick(Clock clock, u64 count = 1);

} // namespace ModLoader::Timer

