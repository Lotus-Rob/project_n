#pragma once

/// @file
/// @ingroup core
/// The run of failed frames the renderer is in, with the error that began it.

#include "core/error.hpp"
#include "core/repeated_failure.hpp"
#include "core/types.hpp"

#include <utility>

namespace core {

/// A run of frames that failed in a row: logged at its start and its end, and holding the error that began it for as long as
/// it lasts. A run ends when a frame is drawn or can't be judged (a minimised window).
class FrameFailures {
public:
    /// Records a failed frame. Returns whether to log it (the run's first), which also keeps `error` as the run's.
    [[nodiscard]] bool Fail(Error error) {
        const bool first = m_failures.Fail();
        if (first) {
            m_first = std::move(error);
        }
        return first;
    }

    /// Ends the run, forgetting its error. Returns how many frames in a row it held (0 when none had failed).
    [[nodiscard]] u64 End() {
        m_first = Error{};
        return m_failures.Succeed();
    }

    /// How many frames in a row have failed (0 when none).
    [[nodiscard]] u64 Failures() const {
        return m_failures.Failures();
    }

    /// The first error of the current run; an empty message when none has failed.
    [[nodiscard]] const Error& First() const {
        return m_first;
    }

private:
    RepeatedFailure m_failures;
    Error m_first{};
};

} // namespace core
