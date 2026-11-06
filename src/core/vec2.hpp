#pragma once

/// @file
/// @ingroup core
/// A 2D vector of f32: positions, velocities and sizes in world units (reference pixels, design
/// doc 0002 §8.10), x to the right and y down.

#include "core/types.hpp"

#include <cmath>

namespace core {

/// A 2D vector in world units.
struct Vec2 {
    /// The horizontal component, growing to the right.
    f32 x = 0.0F;
    /// The vertical component, growing downwards.
    f32 y = 0.0F;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.x, self.y);
    }

    /// The component-wise sum.
    [[nodiscard]] friend constexpr Vec2 operator+(Vec2 lhs, Vec2 rhs) {
        return { .x = lhs.x + rhs.x, .y = lhs.y + rhs.y };
    }
    /// The component-wise difference.
    [[nodiscard]] friend constexpr Vec2 operator-(Vec2 lhs, Vec2 rhs) {
        return { .x = lhs.x - rhs.x, .y = lhs.y - rhs.y };
    }
    /// `vec` scaled by `factor`.
    [[nodiscard]] friend constexpr Vec2 operator*(Vec2 vec, f32 factor) {
        return { .x = vec.x * factor, .y = vec.y * factor };
    }
};

/// Whether both of `vec`'s components are finite: neither NaN nor infinite.
[[nodiscard]] inline bool IsFinite(Vec2 vec) {
    return std::isfinite(vec.x) && std::isfinite(vec.y);
}

} // namespace core
