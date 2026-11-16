#pragma once

/// @file
/// @ingroup app
/// The target cursor: a tile of the world map the player steers by keyboard or gamepad, then walks to. Window-free:
/// tested with constructed arguments.

#include "core/frame.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "core/world_map.hpp"

#include <optional>

namespace app {

/// How long a movement input is held before the cursor first repeats its step, in seconds.
inline constexpr f32 CURSOR_FIRST_REPEAT_SECONDS = 0.3F;
/// How long between the repeats after the first, in seconds.
inline constexpr f32 CURSOR_REPEAT_SECONDS = 0.15F;
/// How far an axis of Bindings::Move() (rescaled past its dead zone, so about 0.6 of a stick's raw travel) must reach for
/// the cursor to count it as pressed.
inline constexpr f32 CURSOR_PRESS_THRESHOLD = 0.5F;

/// The tile the cursor starts on: the one under the cultivator's `foot` on `map`; none off the world map (`worldMap`
/// false) or without a cultivator.
[[nodiscard]] std::optional<u32> CursorStart(bool worldMap, std::optional<core::Vec2> foot, const core::WorldMap& map);

/// The cursor's tile, and how a held movement input steps it.
class TargetCursor {
public:
    /// Shows the cursor on `tile` of a map `columns` by `rows` tiles, row by row. The direction `move` already holds
    /// counts as held: its first step needs a fresh press or CURSOR_FIRST_REPEAT_SECONDS.
    void Show(u32 tile, u32 columns, u32 rows, core::Vec2 move = {});

    /// Hides the cursor.
    void Hide();

    /// Whether the cursor shows.
    [[nodiscard]] bool Shown() const {
        return m_tile.has_value();
    }

    /// Steers the cursor by `move` (each axis past CURSOR_PRESS_THRESHOLD counts as pressed, so a diagonal steps both
    /// ways at once) held for `seconds` more: a new direction steps one tile at once, the same one held again after
    /// CURSOR_FIRST_REPEAT_SECONDS, then every CURSOR_REPEAT_SECONDS, at most one step a frame. The cursor stops at the map's
    /// edge. Does nothing while hidden.
    void Steer(core::Vec2 move, f32 seconds);

    /// The cursor's tile, none while hidden.
    [[nodiscard]] std::optional<u32> Tile() const {
        return m_tile;
    }

    /// The movement to send the game for the held `move`: none while the cursor shows, and after a Confirm() until `move`
    /// has been zero once, so a direction still held doesn't end the route just set. Call it every frame.
    [[nodiscard]] core::Vec2 Walk(core::Vec2 move);

    /// WalkTo the middle of the cursor's tile on `map`, hiding the cursor; none while hidden.
    [[nodiscard]] std::optional<core::Action> Confirm(const core::WorldMap& map);

private:
    void Step(i32 columns, i32 rows);

    std::optional<u32> m_tile = std::nullopt;
    u32 m_columns = 0;
    u32 m_rows = 0;
    i32 m_stepColumns = 0; // the direction held, as the last Steer() saw it
    i32 m_stepRows = 0;
    f32 m_wait = 0.0F;           // seconds until the next repeat
    bool m_awaitRelease = false; // a Confirm() happened and movement hasn't been at rest since
};

} // namespace app
