#pragma once

/// @file
/// @ingroup core
/// Logging a failure that repeats every frame without logging every frame.

#include "core/types.hpp"

#include <utility>

namespace core {

/// Follows an operation retried every frame, such as drawing, through its failures, so that a run
/// of them is logged at its start and its end rather than at every frame.
class RepeatedFailure {
public:
    /// Records a failure. Returns whether to log it.
    [[nodiscard]] bool Fail() {
        return m_failures++ == 0;
    }

    /// Records a success. Returns how many failures in a row it ended (0 when there were none).
    [[nodiscard]] u64 Succeed() {
        return std::exchange(m_failures, 0);
    }

private:
    u64 m_failures = 0;
};

} // namespace core
