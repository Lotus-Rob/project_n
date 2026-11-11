#pragma once

/// @file
/// @ingroup core
/// A volley of flying swords: its shape, the numbers a cast of the ability reads from the data, and the path every sword
/// follows as a function of time. The logic (arrival ticks, plunge points) and the renderer (the exact render time) share
/// the path, so a sword is drawn where the simulation says it is.

#include "core/archive.hpp"
#include "core/stable_id.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

#include <cmath>
#include <span>

namespace core {

/// The most swords a volley has: the overlay buffer holds two instances a sword.
inline constexpr u32 MAX_VOLLEY_COUNT = 2'000;

/// How a volley is laid out and flies, from `abilities.yaml`. All times are simulation ticks.
struct VolleyShape {
    /// How many swords (or bolts) the volley has, at most MAX_VOLLEY_COUNT.
    u32 count = 0;
    /// The radius of the ring they burst out in, in world units.
    f32 ringRadius = 0.0F;
    /// Ticks between the first sword leaving the ring and the last.
    u32 staggerTicks = 0;
    /// Ticks a sword takes to move from the caster to its place on the ring.
    u32 outTicks = 0;
    /// Ticks a sword flies from the ring to its target; 0 means "from the distance and AbilityData::speed" (the bolt).
    u32 flightTicks = 0;
    /// How far a sword's path bends sideways at most, in world units: a long flight bends nearly this far, a short one
    /// less (SwordAt()). 0 flies straight (the bolt).
    f32 sweep = 0.0F;
    /// Ticks a sword takes to fade after its hit.
    u32 fadeTicks = 0;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.count, self.ringRadius, self.staggerTicks, self.outTicks, self.flightTicks, self.sweep, self.fadeTicks);
    }

    /// Whether the count is within the limit and every distance is finite and not negative; what a loaded shape must satisfy.
    [[nodiscard]] bool Valid() const {
        return count <= MAX_VOLLEY_COUNT && std::isfinite(ringRadius) && ringRadius >= 0.0F && std::isfinite(sweep) &&
               sweep >= 0.0F;
    }
};

/// One cast of a volley: everything its swords' paths depend on but their targets, which move.
struct Volley {
    /// The ability use its hits and its end belong to (core::AbilityUse()).
    u64 use = 0;
    /// The tick it was cast on: the first sword launches then at the earliest.
    u64 startTick = 0;
    /// Its own random numbers' seed (core::Random): each sword's launch, place on the ring and bend.
    u64 seed = 0;
    /// Where the swords burst out from: the caster, in world units.
    Vec2 origin{};
    /// The point it was cast at: on the ring, each sword turns to face its flight towards it.
    Vec2 point{};
    /// Its shape as it was cast: new game data never changes a volley in flight.
    VolleyShape shape{};

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.use, self.startTick, self.seed, self.origin, self.point, self.shape);
    }

    /// Whether its shape is valid and its points finite; what a loaded volley must satisfy.
    [[nodiscard]] bool Valid() const {
        return shape.Valid() && IsFinite(origin) && IsFinite(point);
    }
};

/// What a volley's swords fly at: a tagged enemy, or the cast's point when nothing was tagged.
struct VolleyTarget {
    /// The enemy; zero for the point itself, when nothing was tagged.
    StableId entity{};
    /// Where the swords aim at it at the previous tick, in world units.
    Vec2 previous{};
    /// Where the swords aim at it at this tick; frozen where it died.
    Vec2 current{};
    /// Whether it can still be hit; never for the point.
    bool alive = false;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.entity, self.previous, self.current, self.alive);
    }
};

/// Where a sword is at a moment, and how to draw it.
struct SwordPose {
    /// Its position, in world units.
    Vec2 position{};
    /// The way it points, unit length: the way it flies, or on the ring, the way it is turning to.
    Vec2 heading{ .x = 1.0F, .y = 0.0F };
    /// How much of it shows: 0 before its launch, 1 from then until its arrival, falling to 0 over the shape's
    /// `fadeTicks` after.
    f32 shown = 0.0F;
};

/// The tick `sword` (below the shape's count) leaves the caster: the swords stream out over the shape's `staggerTicks`
/// from the volley's start, in an order drawn from its seed.
[[nodiscard]] u64 LaunchTick(const Volley& volley, u32 sword);

/// The tick `sword` (below the shape's count) reaches its target: its launch, then `outTicks` to the ring and
/// `flightTicks` to the target.
[[nodiscard]] u64 ArrivalTick(const Volley& volley, u32 sword);

/// The tick the volley's last sword arrives: the latest ArrivalTick(), or the start for a volley of no swords.
[[nodiscard]] u64 LastArrival(const Volley& volley);

/// Where `sword` (below the shape's count) is at `tick`, a simulation tick or a render time between two, flying at `aim`,
/// its target's position at that moment. Before its launch it waits unseen at the origin. For `outTicks` it bursts out,
/// pointing outwards and slowing, to its place on a ring of `ringRadius` round the origin, and turns there to face its
/// flight. For `flightTicks` it flies a curve that bends to the same side for every sword (so the ring swirls) and ends
/// at `aim`, wherever that moves, speeding up and then slowing a little. After its arrival it stays at `aim` and fades.
/// Neither position nor heading jumps while the aim moves smoothly. A volley of no ring and no sweep (the bolt) flies
/// straight. A pure function: the same inputs give the same bits on every thread.
[[nodiscard]] SwordPose SwordAt(const Volley& volley, u32 sword, f64 tick, Vec2 aim);

/// Which of a volley's `targets` (not empty) `sword` flies at: its own, `sword` modulo their count, while that's alive;
/// when it's dead, one of the living ones, spread evenly over them, so only a dead target's swords move; when none is
/// alive, its own (frozen where it died), where the sword plunges into the ground.
[[nodiscard]] u32 SwordTarget(u32 sword, std::span<const VolleyTarget> targets);

} // namespace core
