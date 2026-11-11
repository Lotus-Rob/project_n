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
    /// Its own random numbers' seed (core::Random): each sword's launch, place on the ring and bend. The cast draws it from
    /// core::Random with a purpose of its own and the state counter, so the volley's draws, which key on this seed with
    /// purposes local to the path, share no numbers with the game's.
    u64 seed = 0;
    /// Where the swords burst out from: the caster, in world units.
    Vec2 origin{};
    /// The point it was cast at: on the ring, each sword turns to face its flight towards it.
    Vec2 point{};
    /// Its shape as it was cast: new game data never changes a volley in flight. The cast resolves `flightTicks` (for the
    /// bolt, ⌈distance / speed⌉), so here it is always the flight's length in ticks: 0 means it arrives on reaching the
    /// ring, never "from the speed".
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
    /// Where the swords aim at it at this tick. From its death on, both stay where it died.
    Vec2 current{};
    /// The tick it stopped being a target (it died, or for the point, the cast's tick); 0 while it lives. Simulation ticks
    /// count from 1 (core::Simulation::Tick), so 0 is free to mean alive. The logic sets it after that tick's arrivals
    /// have resolved: a target dying on a tick still takes the hits that arrive on it.
    u64 diedTick = 0;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.entity, self.previous, self.current, self.diedTick);
    }

    /// Whether it can still be hit at `tick`, a simulation tick or a render time: it hasn't died by then.
    [[nodiscard]] constexpr bool AliveAt(f64 tick) const {
        return diedTick == 0 || static_cast<f64>(diedTick) > tick;
    }
};

/// The fewest ticks a sword takes to swing its aim from a target that died to the one it moves on to, so it turns
/// visibly rather than jumping: about an eighth of a second.
inline constexpr f64 RETARGET_TICKS = 8.0;
/// The fastest a swing moves the aim on average, in world units a tick (about a sword's speed in flight): a swing over a
/// longer way takes longer than RETARGET_TICKS, so the sword never darts, though never past the sword's arrival.
inline constexpr f64 RETARGET_SPEED = 16.0;
/// How many ticks before its arrival a sword's target must die for the sword to move on to another. A later death leaves
/// it on the dead one, where it plunges: too close to arrival to turn visibly, and a sword that has arrived never moves.
inline constexpr u64 COMMIT_TICKS = 16;
static_assert(COMMIT_TICKS == 2 * static_cast<u64>(RETARGET_TICKS), "a sword that moves on has two swings' time to turn");

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

/// The tick the volley's last sword arrives: the latest ArrivalTick(), or the start for a volley of no swords. It draws
/// every sword's launch, so it costs O(count): call it once per volley, not every tick.
[[nodiscard]] u64 LastArrival(const Volley& volley);

/// Where `sword` (below the shape's count) is at `tick`, a simulation tick or a render time between two, flying at `aim`
/// (SwordAim()). Before its launch it waits unseen at the origin. For `outTicks` it bursts out, pointing outwards and
/// slowing, to its place on a ring of `ringRadius` round the origin, and turns there to face its flight. For
/// `flightTicks` it flies a curve that bends to the same side for every sword (so the ring swirls) and ends at `aim`,
/// wherever that moves, speeding up and then slowing a little. After its arrival it stays at `aim` and fades. A volley of
/// no ring and no sweep (the bolt) flies straight. A pure function: the same inputs give the same bits on every thread.
///
/// Its position never jumps while the aim moves continuously. Its heading turns at a bounded rate too, but no rule that
/// depends only on the present aim can promise that everywhere, and it can flip: in flight, when the aim passes over
/// the point it flew from on the ring (the target is then behind it); on the ring, while it turns, when the aim moves
/// to the far side of that point from the volley's `point`.
[[nodiscard]] SwordPose SwordAt(const Volley& volley, u32 sword, f64 tick, Vec2 aim);

/// Which of a volley's `targets` (not empty, as tagged at its cast) `sword` flies at, at `tick`. It starts at its own,
/// `sword` modulo their count. When the one it flies at dies at least COMMIT_TICKS before the sword's arrival, it moves
/// on to one of the targets alive at that tick, chosen so the swords that move together spread evenly over them; the
/// others keep theirs. It repeats at each such death of the one it flies at. When the one it flies at dies later than
/// that, or none is alive at its death, it keeps the dead one and plunges into the ground at its frozen position. So the
/// target in force at the arrival tick is the one the sword strikes, and it never changes after.
///
/// At the arrival tick the logic hits that target if it was alive when the tick's arrivals resolved: arrivals resolve
/// before the tick's deaths, so a target dying on that tick still takes the hit (VolleyTarget::diedTick).
[[nodiscard]] u32 SwordTarget(const Volley& volley, u32 sword, u64 tick, std::span<const VolleyTarget> targets);

/// The aim `sword` flies at, for SwordAt() at the same moment: the moment `RenderTick(simulationTick, alpha)`, with the
/// targets as `simulationTick` left them (the logic passes its tick and 1; the renderer the drawn snapshot's tick and the
/// frame's alpha, so the two can't disagree). It is the position of its target (SwordTarget()), between the target's
/// `previous` and `current` by `alpha`. When the sword moves on from a dead target, its aim eases from where it was then
/// to the new target, over RETARGET_TICKS or the way's length at RETARGET_SPEED, whichever is longer, but always ending
/// by the sword's arrival: the aim, and with it the sword, never jumps, and at the arrival tick it is exactly on the
/// target it strikes.
[[nodiscard]] Vec2 SwordAim(
    const Volley& volley,
    u32 sword,
    u64 simulationTick,
    f32 alpha,
    std::span<const VolleyTarget> targets
);

} // namespace core
