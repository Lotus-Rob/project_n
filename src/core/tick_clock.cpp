#include "core/tick_clock.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace core {

TickBurst TickClock::Advance(u64 nowNs) {
    if (nowNs < NextNs()) {
        return {};
    }
    // The last tick due: the largest k with TickTime(m_originNs, k) <= nowNs.
    const u64 last = (((nowNs - m_originNs + 1) * TICKS_PER_SECOND) - 1) / NS_PER_SECOND;
    TickBurst burst{ .ticks = last - m_index + 1 };
    if (burst.ticks > MAX_CATCH_UP) {
        m_originNs = nowNs - TickTime(0, MAX_CATCH_UP - 1);
        m_index = 0;
        burst = { .ticks = MAX_CATCH_UP, .droppedTicks = burst.ticks - MAX_CATCH_UP };
    }
    if (burst.ticks > 1 && !m_lateSinceNs) {
        m_lateSinceNs = nowNs;
    }
    else if (burst.ticks == 1) {
        m_lateSinceNs.reset();
    }
    m_index += burst.ticks;
    burst.scheduledNs = TickTime(m_originNs, m_index - 1);
    return burst;
}

void TickClock::Restart(u64 nowNs) {
    m_originNs = nowNs;
    m_index = 0;
    m_lateSinceNs.reset();
}

} // namespace core
