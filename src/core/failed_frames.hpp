#pragma once

/// @file
/// @ingroup core
/// Telling a graphics card that stopped drawing from a few frames that failed.

#include "core/tick_clock.hpp"
#include "core/types.hpp"

#include <optional>

namespace core {

/// Frames a run of failures must hold before the graphics card counts as lost.
inline constexpr u64 LOST_FRAMES = 120;
/// Time a run of failures must span before the graphics card counts as lost.
inline constexpr u64 LOST_NS = 3 * NS_PER_SECOND;

/// Failed frames a run must hold before the loop waits after each one. A failed acquire returns in about 6 us, so ten of them
/// spin harmlessly, while a lost card would otherwise spin a core; one transient failure costs no dropped frame.
inline constexpr u64 PAUSE_AFTER_FRAMES = 10;

/// Whether the loop should wait after a frame, with `failuresInARow` failed frames in the current run.
[[nodiscard]] constexpr bool PausesAfter(u64 failuresInARow) {
    return failuresInARow >= PAUSE_AFTER_FRAMES;
}

/// Decides when frames that keep failing mean the graphics card has stopped drawing. A run must hold both LOST_FRAMES frames
/// and LOST_NS of time: an unpaced loop fails 120 frames in milliseconds, and a compositor at 1 Hz takes minutes to reach them.
class FailedFrames {
public:
    /// Whether the run of `failuresInARow` failed frames, read at `nowNs`, has lost the graphics card. A count of 0 ends
    /// the run; the run's start is the first call with a count above 0 after that.
    [[nodiscard]] bool Lost(u64 failuresInARow, u64 nowNs) {
        if (failuresInARow == 0) {
            m_begun.reset();
            return false;
        }
        if (!m_begun) {
            m_begun = nowNs;
        }
        return failuresInARow >= LOST_FRAMES && nowNs - *m_begun >= LOST_NS;
    }

    /// A frame that can't be judged (a minimised window draws nothing and acquires nothing) ends the run as a success does: the
    /// card's state is unknown then, and one still lost after the restore fails a fresh run's worth.
    void Unjudged() {
        m_begun.reset();
    }

    /// How long the run has lasted at `nowNs`; 0 when there is none.
    [[nodiscard]] u64 RunNs(u64 nowNs) const {
        return m_begun ? nowNs - *m_begun : 0;
    }

private:
    std::optional<u64> m_begun = std::nullopt;
};

} // namespace core
