#pragma once

/// @file
/// @ingroup app
/// What a left click on the picture sends. Pure: tested with constructed arguments.

#include "core/frame.hpp"
#include "core/stable_id.hpp"
#include "core/vec2.hpp"

namespace app {

/// The action a click sends: Select of `picked` when something was under the pointer; else on the world map (`worldMap`)
/// WalkTo `worldPoint`; else Select of nothing, which clears the selection.
[[nodiscard]] core::Action ClickAction(bool worldMap, core::StableId picked, core::Vec2 worldPoint);

} // namespace app
