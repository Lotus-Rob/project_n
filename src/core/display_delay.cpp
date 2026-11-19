#include "core/display_delay.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace core {

namespace {

// Rounded to the nearest nanosecond, so a rate like 0.02 doesn't land one short of an exact value.
u64 Step(u64 elapsedNs, f64 rate) {
    return static_cast<u64>(std::llround(static_cast<f64>(elapsedNs) * rate));
}

} // namespace

void DisplayDelay::Observe(u64 latenessNs) {
    m_samples[m_next] = latenessNs;
    m_next = (m_next + 1) % DISPLAY_DELAY_WINDOW;
    m_count = std::min(m_count + 1, DISPLAY_DELAY_WINDOW);
    ++m_observed;

    auto sorted = m_samples;
    const std::span<u64> held = std::span{ sorted }.first(m_count);
    const std::span<u64>::iterator rank = std::next(held.begin(), static_cast<std::ptrdiff_t>(m_count * 99 / 100));
    std::nth_element(held.begin(), rank, held.end());
    m_target = std::clamp(*rank + DISPLAY_DELAY_MARGIN_NS, DISPLAY_DELAY_FLOOR_NS, TICK_NS);
}

u64 DisplayDelay::Advance(u64 nowNs) {
    if (!m_started) {
        m_started = true;
        m_lastNs = nowNs;
        return m_current;
    }
    const u64 elapsed = nowNs > m_lastNs ? nowNs - m_lastNs : 0;
    m_lastNs = std::max(m_lastNs, nowNs);

    if (m_current < m_target) {
        const u64 step = Step(elapsed, DISPLAY_DELAY_RISE_RATE);
        m_current = std::min(m_current + step, m_target);
    }
    else if (m_current > m_target && m_observed >= DISPLAY_DELAY_SETTLED) {
        const u64 step = Step(elapsed, DISPLAY_DELAY_FALL_RATE);
        m_current = std::max(m_current > step ? m_current - step : 0, m_target);
    }
    m_lowest = std::min(m_lowest, m_current);
    m_highest = std::max(m_highest, m_current);
    return m_current;
}

} // namespace core
