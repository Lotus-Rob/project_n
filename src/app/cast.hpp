#pragma once

/// @file
/// @ingroup app
/// Casting (the phase 6 chunk 4 spec): which controls cast which slot, and when a press is a cast. Every press is its
/// own Cast action, never merged, repeated while held or debounced here: the simulation decides what a press does,
/// so a combo needs no change to the action or to this module. Pure: tested with constructed controls.

#include "app/bindings.hpp"
#include "core/abilities.hpp"
#include "core/frame.hpp"
#include "core/vec2.hpp"

#include <optional>

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

} // namespace app
