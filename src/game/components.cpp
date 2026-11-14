#include "game/components.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/math.hpp"

namespace game {

namespace {

constexpr f32 WOBBLES_PER_LAP = 3.0F;

} // namespace

core::Vec2 OrbitPoint(const Orbit& orbit, u64 tick) {
    // In f64 and reduced to one turn: an f32 angle loses whole ticks within hours, and Sin and Cos are NaN past 2^20
    // quarter turns.
    const f64 angle = std::fmod(
        static_cast<f64>(orbit.phase) + (static_cast<f64>(orbit.speed) * static_cast<f64>(tick)), 2 * std::numbers::pi
    );
    const f64 radius = static_cast<f64>(orbit.radius) +
                       (static_cast<f64>(orbit.wobble) * core::math::Sin(static_cast<f64>(WOBBLES_PER_LAP) * angle));
    return {
        .x = orbit.centre.x + static_cast<f32>(radius * core::math::Cos(angle)),
        .y = orbit.centre.y + static_cast<f32>(radius * core::math::Sin(angle) * static_cast<f64>(VIEW_TILT)),
    };
}

} // namespace game
