#include "app/target_cursor.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"

namespace app {

namespace {

i32 Pressed(f32 axis) {
    if (axis > CURSOR_PRESS_THRESHOLD) {
        return 1;
    }
    return axis < -CURSOR_PRESS_THRESHOLD ? -1 : 0;
}

/// `value` moved by `delta` and kept inside 0 to `count` - 1.
u32 Moved(u32 value, i32 delta, u32 count) {
    const i64 moved = std::clamp<i64>(i64{ value } + delta, 0, i64{ count } - 1);
    return static_cast<u32>(moved);
}

} // namespace

void TargetCursor::Show(u32 tile, u32 columns, u32 rows) {
    m_tile = tile;
    m_columns = columns;
    m_rows = rows;
    m_stepColumns = 0;
    m_stepRows = 0;
    m_wait = 0.0F;
}

void TargetCursor::Hide() {
    m_tile = std::nullopt;
}

void TargetCursor::Step(i32 columns, i32 rows) {
    const u32 tile = m_tile.value_or(0);
    const u32 column = Moved(tile % m_columns, columns, m_columns);
    const u32 row = Moved(tile / m_columns, rows, m_rows);
    m_tile = (row * m_columns) + column;
}

void TargetCursor::Steer(core::Vec2 move, f32 seconds) {
    PN_PROFILE_ZONE();
    if (!m_tile) {
        return;
    }
    const i32 columns = Pressed(move.x);
    const i32 rows = Pressed(move.y);
    if (columns == 0 && rows == 0) {
        m_stepColumns = 0;
        m_stepRows = 0;
        return;
    }
    if (columns != m_stepColumns || rows != m_stepRows) {
        m_stepColumns = columns;
        m_stepRows = rows;
        m_wait = CURSOR_FIRST_REPEAT_SECONDS;
        Step(columns, rows);
        return;
    }
    m_wait -= seconds;
    while (m_wait <= 0.0F) {
        Step(columns, rows);
        m_wait += CURSOR_REPEAT_SECONDS;
    }
}

std::optional<core::Action> TargetCursor::Confirm(const core::WorldMap& map) {
    if (!m_tile) {
        return std::nullopt;
    }
    core::Action action{ .kind = core::ActionKind::WalkTo, .point = map.TileCentre(*m_tile) };
    Hide();
    return action;
}

} // namespace app
