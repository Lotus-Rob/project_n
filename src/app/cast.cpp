#include "app/cast.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace app {

namespace {

struct SlotControl {
    Control control = Control::Primary;
    core::Slot slot = core::Slot::Generator;
};
constexpr std::array SLOT_CONTROLS{
    SlotControl{ .control = Control::Primary, .slot = core::Slot::Generator },
    SlotControl{ .control = Control::Secondary, .slot = core::Slot::Spender },
    SlotControl{ .control = Control::Movement, .slot = core::Slot::Movement },
    SlotControl{ .control = Control::Special1, .slot = core::Slot::Special1 },
    SlotControl{ .control = Control::Special2, .slot = core::Slot::Special2 },
    SlotControl{ .control = Control::Special3, .slot = core::Slot::Special3 },
    SlotControl{ .control = Control::Special4, .slot = core::Slot::Special4 },
    SlotControl{ .control = Control::Ultimate, .slot = core::Slot::Ultimate },
};

} // namespace

std::optional<core::Slot> SlotOf(Control control) {
    const std::span controls{ SLOT_CONTROLS }; // a span's iterator is a class everywhere, unlike std::array's
    const auto found = std::ranges::find(controls, control, &SlotControl::control);
    if (found == controls.end()) {
        return std::nullopt;
    }
    return found->slot;
}

bool SelectsInstead(Control control, bool combat, bool alt) {
    return control == Control::Primary && (!combat || alt);
}

std::optional<core::Action> CastAction(Control control, bool pressed, bool combat, bool alt, std::optional<core::Vec2> point) {
    const std::optional<core::Slot> slot = SlotOf(control);
    if (!pressed || !combat || !slot || !point || SelectsInstead(control, combat, alt)) {
        return std::nullopt;
    }
    return core::Action{ .kind = core::ActionKind::Cast, .slot = *slot, .point = *point };
}

std::optional<core::Action> TimedCast(
    const core::CasterView& caster,
    const core::Abilities& abilities,
    u64 tick,
    core::Vec2 point
) {
    const core::AbilityKind swords = caster.loadout.at(std::to_underlying(core::Slot::Special1));
    if (swords == core::AbilityKind::None) {
        return std::nullopt;
    }
    const auto ready = [&caster, tick](core::Slot slot) { return caster.readyTick.at(std::to_underlying(slot)) <= tick; };
    const auto cast = [point](core::Slot slot) {
        return core::Action{ .kind = core::ActionKind::Cast, .slot = slot, .point = point };
    };
    if (caster.qi < abilities.Of(swords).qiCost) {
        return ready(core::Slot::Generator) ? std::optional{ cast(core::Slot::Generator) } : std::nullopt;
    }
    return ready(core::Slot::Special1) ? std::optional{ cast(core::Slot::Special1) } : std::nullopt;
}

CastLatency::CastLatency(core::AbilityKind ability)
  : m_ability{ ability } {
}

void CastLatency::Pressed(u64 nowNs) {
    m_presses.push_back(nowNs);
}

std::optional<u64> CastLatency::Drawn(std::span<const core::VolleyView> volleys, u64 nowNs) {
    std::erase_if(m_presses, [nowNs](u64 pressed) { return nowNs - pressed > CAST_LATENCY_LIMIT_NS; });
    m_drawing.clear();
    bool fresh = false;
    for (const core::VolleyView& view : volleys) {
        if (view.ability == m_ability) {
            m_drawing.push_back(view.volley.use);
            fresh = fresh || !std::ranges::contains(m_drawn, view.volley.use);
        }
    }
    std::swap(m_drawn, m_drawing);
    if (!fresh || m_presses.empty()) {
        return std::nullopt;
    }
    const u64 pressed = m_presses.front();
    m_presses.erase(m_presses.begin());
    return nowNs - pressed;
}

} // namespace app
