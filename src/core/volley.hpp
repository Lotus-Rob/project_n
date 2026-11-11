#pragma once

/// @file
/// @ingroup core
/// A volley of flying swords: its shape, the numbers a cast of the ability reads from the data.

#include "core/archive.hpp"
#include "core/types.hpp"

#include <cmath>

namespace core {

/// How a volley is laid out and flies, from `abilities.yaml`. All times are simulation ticks.
struct VolleyShape {
    /// How many swords (or bolts) the volley has.
    u32 count = 0;
    /// The radius of the ring they burst out in, in world units.
    f32 ringRadius = 0.0F;
    /// Ticks between the first sword leaving the ring and the last.
    u32 staggerTicks = 0;
    /// Ticks a sword takes to move from the caster to its place on the ring.
    u32 outTicks = 0;
    /// Ticks a sword flies from the ring to its target; 0 means "from the distance and AbilityData::speed" (the bolt).
    u32 flightTicks = 0;
    /// How far a sword's path bends sideways, in world units.
    f32 sweep = 0.0F;
    /// Ticks a sword takes to fade after its hit.
    u32 fadeTicks = 0;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.count, self.ringRadius, self.staggerTicks, self.outTicks, self.flightTicks, self.sweep, self.fadeTicks);
    }

    /// Whether every distance is finite and not negative; what a loaded shape must satisfy.
    [[nodiscard]] bool Valid() const {
        return std::isfinite(ringRadius) && ringRadius >= 0.0F && std::isfinite(sweep) && sweep >= 0.0F;
    }
};

} // namespace core
