#pragma once

/// @file
/// @ingroup platform
/// The clock both threads share, and precise waiting on it.

#include "core/types.hpp"

namespace platform {

/// Nanoseconds on a monotonic clock shared by every thread; its zero is arbitrary.
[[nodiscard]] u64 NowNs();

/// Returns at `deadlineNs` on NowNs()'s clock, or at once if it has passed: sleeps until about a
/// millisecond before, then waits precisely (busy waiting if necessary).
void WaitUntilNs(u64 deadlineNs);

} // namespace platform
