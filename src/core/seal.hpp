#pragma once

/// @file
/// @ingroup core
/// A seal in play (the phase 6 chunk 4b spec, "In the simulation"): which seal, where, how large, when and of which
/// element, as the simulation records and saves it. Nothing about its look is here: the looks (`assets/data/seals.yaml`)
/// find a seal by its key.

#include "core/archive.hpp"
#include "core/hit.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace core {

/// Names a seal: the FNV-1a 64 hash of its name (SealKeyOf()).
using SealKey = u64;

/// The key of the seal named `name`: FNV-1a 64 of its bytes, the same on every run, build and platform, so game data and
/// the looks, each naming a seal, agree on it.
[[nodiscard]] constexpr SealKey SealKeyOf(std::string_view name) {
    constexpr u64 OFFSET_BASIS = 0xcbf2'9ce4'8422'2325ULL;
    constexpr u64 PRIME = 0x0000'0100'0000'01b3ULL;
    u64 hash = OFFSET_BASIS;
    for (const char character : name) {
        hash ^= static_cast<u8>(character);
        hash *= PRIME;
    }
    return hash;
}

/// Seal::endTick for a seal that never ends.
inline constexpr u64 NEVER_ENDS = std::numeric_limits<u64>::max();

/// A seal in play: a small record, as a volley is, which the main thread draws from its look and the render time.
struct Seal {
    /// Which seal it is (SealKeyOf() its name).
    SealKey key = 0;
    /// Its centre, in world units: a fixed point.
    Vec2 point{};
    /// Its outer radius, in world units: above 0. Its look's proportions scale to it.
    f32 size = 0.0F;
    /// The simulation tick it starts at.
    u64 startTick = 0;
    /// The simulation tick it ends at, from `startTick` on: it is in play from `startTick` to the tick before this one.
    /// NEVER_ENDS for one that never ends.
    u64 endTick = NEVER_ENDS;
    /// The element it was raised with, when `hasElement`: a colour slot of its look naming `element` takes this element's
    /// colour.
    HitKind element = HitKind::Damage;
    /// Whether it has an element: a seal raised in play does; the showcase's don't.
    bool hasElement = false;
    /// Whether the arena's scene content placed it (the showcase), republished every tick rather than state: never saved
    /// or hashed.
    bool showcase = false;

    /// Lists its saved fields for an archive (core/archive.hpp): all but `showcase`.
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.key, self.point, self.size, self.startTick, self.endTick, self.element, self.hasElement);
    }

    /// Whether a value read from a save can be used: a finite point, a finite size above 0, an end not before its start
    /// and a known element.
    [[nodiscard]] bool Valid() const {
        return IsFinite(point) && std::isfinite(size) && size > 0.0F && endTick >= startTick &&
               std::to_underlying(element) < HIT_KINDS;
    }
};

/// What casting an ability raises under its caster (AbilityData::seal): which seal and how large. Game data, hashed.
struct SealCast {
    /// Which seal (SealKeyOf() the name in abilities.yaml).
    SealKey key = 0;
    /// Its outer radius, in world units: above 0.
    f32 size = 0.0F;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.key, self.size);
    }

    /// Whether its size is finite and above 0.
    [[nodiscard]] bool Valid() const {
        return std::isfinite(size) && size > 0.0F;
    }
};

} // namespace core
