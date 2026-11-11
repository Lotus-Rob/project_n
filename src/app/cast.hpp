#pragma once

/// @file
/// @ingroup app
/// Casting (the phase 6 chunk 4 spec): which controls cast which slot, and when a press is a cast. Every press is its
/// own Cast action, never merged, repeated while held or debounced here: the simulation decides what a press does,
/// so a combo needs no change to the action or to this module. Also the casts `--cast-swords` sends by itself for a
/// profile run, and the time from a press to the first frame drawing its volley. Pure: tested with constructed controls.

#include "app/bindings.hpp"
#include "core/abilities.hpp"
#include "core/frame.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

#include <optional>
#include <span>
#include <vector>

namespace app {

/// The slot `control` casts: the left button the generator, the right the spender, Space the movement, 1 to 4 the
/// specials and 5 the ultimate; none for any other control.
[[nodiscard]] std::optional<core::Slot> SlotOf(Control control);

/// Whether a press of `control` selects what is under the pointer instead of casting: the left button outside a
/// combat scene, or with Alt held (the debug selection, until chunks 5 and 6).
[[nodiscard]] bool SelectsInstead(Control control, bool combat, bool alt);

/// The Cast a press of `control` sends, at the world point `point`: none for a release, outside a combat scene,
/// for a control without a slot, for a press that selects instead, or without a point (the cursor isn't over the window).
[[nodiscard]] std::optional<core::Action> CastAction(
    Control control,
    bool pressed,
    bool combat,
    bool alt,
    std::optional<core::Vec2> point
);

/// The cast `--cast-swords` sends now, at `point`, for the player's casting state `caster` at simulation tick `tick` with
/// the game data's `abilities`: the first special (the flying swords) when that slot is ready and the Qi covers it; else,
/// while the Qi falls short of it, the generator when that slot is ready, so a profile run tops its Qi up the way a
/// player does, changing no rule; else none. None without an ability in the first special's slot.
[[nodiscard]] std::optional<core::Action> TimedCast(
    const core::CasterView& caster,
    const core::Abilities& abilities,
    u64 tick,
    core::Vec2 point
);

/// How long CastLatency waits for a press's volley: a press no volley answers within it (a cast refused for its
/// cooldown, its Qi or a pause) is forgotten, so it never lengthens a later press's latency.
inline constexpr u64 CAST_LATENCY_LIMIT_NS = core::NS_PER_SECOND;

/// The time from a press that casts one ability to the first frame that draws its volley: the input latency of a cast
/// as the player sees it (the phase 6 chunk 4 spec's measurement).
class CastLatency {
public:
    /// Measures the casts of `ability`, whose volleys it watches for.
    explicit CastLatency(core::AbilityKind ability);

    /// A press that casts the ability, at `nowNs` (platform::NowNs()).
    void Pressed(u64 nowNs);

    /// The frame drawn at `nowNs` draws `volleys`: when one of the ability's is among them that no frame drew before,
    /// the time since the oldest press waiting, which no longer waits; else none. A press waiting longer than
    /// CAST_LATENCY_LIMIT_NS is forgotten first.
    [[nodiscard]] std::optional<u64> Drawn(std::span<const core::VolleyView> volleys, u64 nowNs);

private:
    core::AbilityKind m_ability;
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<u64> m_presses{}; // waiting, oldest first
    std::vector<u64> m_drawn{};   // the uses of the ability's volleys the last frame drew
    std::vector<u64> m_drawing{}; // this frame's, kept to reuse its room
    // NOLINTEND(readability-redundant-member-init)
};

} // namespace app
