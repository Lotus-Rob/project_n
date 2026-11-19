#pragma once

/// @file
/// @ingroup core
/// The adaptive display delay D: it follows the 99th percentile of how late recent snapshots were
/// published, so a calm machine shows the world sooner and a hitching one still shows it smoothly.
/// Presentation only: never hashed, saved, recorded or sent to the logic thread. Pure functions of
/// the times and latenesses passed in; nothing here reads a clock.

#include "core/tick_clock.hpp"
#include "core/types.hpp"

#include <array>
#include <cstddef>

namespace core {

/// The lowest D: a margin that survives even a machine whose snapshots are never late.
inline constexpr u64 DISPLAY_DELAY_FLOOR_NS = 2'000'000;
/// Added to the 99th percentile of lateness, for the lateness the window hasn't seen yet.
inline constexpr u64 DISPLAY_DELAY_MARGIN_NS = 1'000'000;
/// Snapshots whose lateness the percentile covers: ten seconds at 60 a second.
inline constexpr std::size_t DISPLAY_DELAY_WINDOW = 600;
/// Snapshots observed before D may fall: the first second after a start or load is slow and
/// unrepresentative.
inline constexpr std::size_t DISPLAY_DELAY_SETTLED = 60;
/// The most D falls, as a share of the time passed: a fall is never noticed as a jump.
inline constexpr f64 DISPLAY_DELAY_FALL_RATE = 0.02;
/// The most D rises, as a share of the time passed: lateness is answered quickly, but not in a jump.
inline constexpr f64 DISPLAY_DELAY_RISE_RATE = 0.10;

/// D over time: observed lateness sets a target, and Advance() moves D toward it at a limited speed.
class DisplayDelay {
public:
    /// Records one snapshot's lateness (published time minus scheduled time; 0 for an early one) and
    /// recomputes the target. Called about 60 times a second, never per frame.
    void Observe(u64 latenessNs);

    /// Moves D toward the target by at most DISPLAY_DELAY_RISE_RATE (up) or DISPLAY_DELAY_FALL_RATE
    /// (down) of the time since the last call, never past the target; it falls only once
    /// DISPLAY_DELAY_SETTLED snapshots were observed. The first call only records `nowNs`. Times
    /// passed in never go back. Returns the new D.
    u64 Advance(u64 nowNs);

    /// D now, in nanoseconds.
    [[nodiscard]] u64 Current() const {
        return m_current;
    }

    /// The lowest D has been since construction.
    [[nodiscard]] u64 Lowest() const {
        return m_lowest;
    }

    /// The highest D has been since construction.
    [[nodiscard]] u64 Highest() const {
        return m_highest;
    }

    /// Where D is heading: the window's 99th percentile plus DISPLAY_DELAY_MARGIN_NS, kept between
    /// DISPLAY_DELAY_FLOOR_NS and TICK_NS; DISPLAY_DELAY_START_NS with nothing observed.
    [[nodiscard]] u64 Target() const {
        return m_target;
    }

private:
    std::array<u64, DISPLAY_DELAY_WINDOW> m_samples{};
    std::size_t m_count = 0;    // samples held, at most the window
    std::size_t m_next = 0;     // where the next sample goes
    std::size_t m_observed = 0; // samples seen since construction
    u64 m_target = DISPLAY_DELAY_START_NS;
    u64 m_current = DISPLAY_DELAY_START_NS;
    u64 m_lowest = DISPLAY_DELAY_START_NS;
    u64 m_highest = DISPLAY_DELAY_START_NS;
    u64 m_lastNs = 0;
    bool m_started = false;
};

} // namespace core
