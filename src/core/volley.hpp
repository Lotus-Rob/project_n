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

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

namespace core {

/// The most swords a volley has: the overlay buffer holds three instances a sword (its core, halo and blade).
inline constexpr u32 MAX_VOLLEY_COUNT = 2'000;

/// The fewest ticks a sword takes to swing its aim from a target that died to the one it moves on to, so it turns
/// visibly rather than jumping: about an eighth of a second.
inline constexpr f64 RETARGET_TICKS = 8.0;
/// The fastest a swing moves the aim on average, in world units a tick (about a sword's speed in flight): a swing over a
/// longer way takes longer than RETARGET_TICKS, so the sword never darts, though never past the sword's arrival.
inline constexpr f64 RETARGET_SPEED = 16.0;
/// The fastest a swing at RETARGET_SPEED moves the aim, in world units a tick: its ease, 3u² − 2u³, peaks at 1.5 times
/// its average speed, halfway through.
inline constexpr f64 RETARGET_PEAK = 1.5 * RETARGET_SPEED;
/// How far from its target's middle a sword's own aim lies at most, in world units (AimOffset()): about a quarter of a
/// fox's height, so swords strike and plunge spread over a creature rather than piled on one point.
inline constexpr f32 AIM_SCATTER = 18.0F;

/// The fewest ticks a volley's commit window has (VolleyShape::commitTicks): two of the shortest swings.
inline constexpr u32 MIN_COMMIT_TICKS = 2 * static_cast<u32>(RETARGET_TICKS);

/// The commit window (VolleyShape::commitTicks) of a volley whose targets were tagged within `radius` world units (finite,
/// not negative) of its point: the ticks a swing across the whole area takes at RETARGET_SPEED,
/// ⌈2 × radius / RETARGET_SPEED⌉, and never fewer than MIN_COMMIT_TICKS. So a sword moving on from one tagged target to
/// another never swings faster than RETARGET_SPEED to reach it by its arrival, unless the two have since moved further
/// apart. The cast calls it. A radius too large for the ticks to count gives the most there are: no sword moves on.
[[nodiscard]] constexpr u32 CommitTicksFor(f32 radius) {
    const f64 diameter = static_cast<f64>(radius) + static_cast<f64>(radius);
    const f64 ticks = diameter / RETARGET_SPEED;
    constexpr u32 MOST = std::numeric_limits<u32>::max();
    if (!(ticks < static_cast<f64>(MOST))) {
        return MOST;
    }
    const u32 whole = ticks > 0.0 ? static_cast<u32>(ticks) : 0;
    return std::max(MIN_COMMIT_TICKS, static_cast<f64>(whole) < ticks ? whole + 1 : whole);
}

/// How a volley is laid out and flies, from `abilities.yaml`. All times are simulation ticks.
///
/// The swords form in waves: rings one after another, the innermost first, each further out than the one before. A
/// sword's place round its ring, how far in or out of the ring it lies, its launch within its wave and how much its
/// flight bends are each drawn from the volley's seed, so the rings are loose bands and the flights a swarm.
struct VolleyShape {
    /// How many swords (or bolts) the volley has, at most MAX_VOLLEY_COUNT.
    u32 count = 0;
    /// How many rings the swords form, one after another (WaveOf()): at least 1, at most MAX_VOLLEY_COUNT. A volley of
    /// one (the bolt) has 1.
    u32 waves = 1;
    /// The radius of the first, innermost ring, in world units.
    f32 ringRadius = 0.0F;
    /// How much further out each ring forms than the one before, in world units.
    f32 ringStep = 0.0F;
    /// How far a sword's place may stray in or out from its ring, as a fraction of the ring's radius, from 0 (a line) to
    /// below 1 (0.3 is a band of ±30%).
    f32 scatter = 0.0F;
    /// How far a sword's place may stray round its ring from an even spacing, either way, in radians.
    f32 angleJitter = 0.0F;
    /// Ticks between the first sword of a wave leaving the caster and the last.
    u32 staggerTicks = 0;
    /// Ticks between one wave's start and the next's.
    u32 waveGapTicks = 0;
    /// Ticks a sword takes to move from the caster to its place on its ring.
    u32 outTicks = 0;
    /// Ticks a sword flies from its ring to its target; 0 means "from the distance and AbilityData::speed" (the bolt).
    u32 flightTicks = 0;
    /// How far a sword's path bends sideways, in world units: a long flight bends nearly this far, a short one less
    /// (SwordAt()), each sword by its own share of it (`sweepJitter`). 0 flies straight (the bolt).
    f32 sweep = 0.0F;
    /// How much a sword's bend may differ from the sweep, either way, as a fraction of it, from 0 to below 1: each sword
    /// bends by a share of the sweep from 1 − sweepJitter to 1 + sweepJitter, always to the same side.
    f32 sweepJitter = 0.0F;
    /// Ticks a sword takes to fade after its hit.
    u32 fadeTicks = 0;
    /// How many ticks before its arrival a sword's target must die for the sword to move on to another (SwordTarget()):
    /// long enough for a swing between any two of the volley's targets at RETARGET_SPEED. The cast resolves it from the
    /// ability's radius (CommitTicksFor()); the data never sets it, so a shape read from the data has 0 here.
    u32 commitTicks = 0;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(
            self.count,
            self.waves,
            self.ringRadius,
            self.ringStep,
            self.scatter,
            self.angleJitter,
            self.staggerTicks,
            self.waveGapTicks,
            self.outTicks,
            self.flightTicks,
            self.sweep,
            self.sweepJitter,
            self.fadeTicks,
            self.commitTicks
        );
    }

    /// Whether the count and the waves are within the limit with at least one wave, every distance and angle is finite and
    /// not negative, and the scatter and the sweep's jitter are below 1; what a shape read from the data must satisfy. The
    /// times the cast resolves, `flightTicks` and `commitTicks`, are checked on the cast (Volley::Valid()).
    [[nodiscard]] bool Valid() const {
        const auto distance = [](f32 value) { return std::isfinite(value) && value >= 0.0F; };
        const auto fraction = [&distance](f32 value) { return distance(value) && value < 1.0F; };
        return count <= MAX_VOLLEY_COUNT && waves >= 1 && waves <= MAX_VOLLEY_COUNT && distance(ringRadius) &&
               distance(ringStep) && fraction(scatter) && distance(angleJitter) && distance(sweep) && fraction(sweepJitter);
    }
};

/// One cast of a volley: everything its swords' paths depend on but their targets, which move.
struct Volley {
    /// The ability use its hits and its end belong to (core::AbilityUse()).
    u64 use = 0;
    /// The tick it was cast on: the first sword launches then at the earliest.
    u64 startTick = 0;
    /// Its own random numbers' seed (core::Random): each sword's launch, place on its ring and bend. The cast draws it from
    /// core::Random with a purpose of its own and the state counter, so the volley's draws, which key on this seed with
    /// purposes local to the path, share no numbers with the game's.
    u64 seed = 0;
    /// Where the swords burst out from: the caster, in world units.
    Vec2 origin{};
    /// The point it was cast at: on the ring, each sword turns to face its flight towards it.
    Vec2 point{};
    /// Its shape as it was cast: new game data never changes a volley in flight. The cast resolves `flightTicks` (for the
    /// bolt, ⌈distance / speed⌉), so here it is always the flight's length in ticks: 0 means it arrives on reaching the
    /// ring, never "from the speed". It resolves `commitTicks` too, which the data never sets, as CommitTicksFor() the
    /// ability's radius, so here it is at least MIN_COMMIT_TICKS.
    VolleyShape shape{};

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.use, self.startTick, self.seed, self.origin, self.point, self.shape);
    }

    /// Whether its shape is valid with its commit window resolved (at least MIN_COMMIT_TICKS) and its points finite; what
    /// a loaded volley must satisfy.
    [[nodiscard]] bool Valid() const {
        return shape.Valid() && shape.commitTicks >= MIN_COMMIT_TICKS && IsFinite(origin) && IsFinite(point);
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

    /// Whether a sword arriving at the tick `arrival`, flying at it then (SwordTarget()), strikes it: it is a creature,
    /// not the point, and it was alive as that tick's arrivals resolved, which is before the tick's deaths. Otherwise
    /// the sword plunges. The logic asks it on the arrival tick, the renderer on any snapshot from then on.
    [[nodiscard]] constexpr bool Struck(u64 arrival) const {
        return entity != StableId{} && (diedTick == 0 || diedTick >= arrival);
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

/// The wave `sword` (below the shape's count) forms in, from 0 (the first, innermost ring) to below the shape's `waves`.
/// The swords split over the waves by index, in order, as evenly as possible: the first `count % waves` waves hold one
/// more than the rest.
[[nodiscard]] u32 WaveOf(const Volley& volley, u32 sword);

/// The tick `sword` (below the shape's count) leaves the caster: its wave (WaveOf()) starts `waveGapTicks` after the one
/// before, the first on the volley's start tick, and the wave's swords stream out over the shape's `staggerTicks` from
/// then, in an order drawn from the seed.
[[nodiscard]] u64 LaunchTick(const Volley& volley, u32 sword);

/// The tick `sword` (below the shape's count) reaches its target: its launch, then `outTicks` to the ring and
/// `flightTicks` to the target.
[[nodiscard]] u64 ArrivalTick(const Volley& volley, u32 sword);

/// The tick the volley's last sword arrives: the latest ArrivalTick(), or the start for a volley of no swords. It draws
/// every sword's launch, so it costs O(count): call it once per volley, not every tick.
[[nodiscard]] u64 LastArrival(const Volley& volley);

/// Where `sword` (below the shape's count) is at `tick`, a simulation tick or a render time between two, flying at `aim`
/// (SwordAim()). Before its launch it waits unseen at the origin. For `outTicks` it spirals out, facing the way it moves
/// and slowing, to its place on its wave's ring round the origin (`ringRadius`, a `ringStep` further for each wave before
/// it, strayed in or out by up to `scatter` of that and round by up to `angleJitter`), sweeping round the origin the way
/// its flight bends, and turns there to face its flight. For `flightTicks` it flies a curve that bends to the same side
/// for every sword (so the rings swirl), by its own share of the sweep (`sweepJitter`), and ends at `aim`, wherever that
/// moves, speeding up and then slowing a little. After its arrival it stays at `aim` and fades. A volley of no ring and
/// no sweep (the bolt) flies straight. A pure function: the same inputs give the same bits on every thread.
///
/// Its position never jumps while the aim moves continuously. Its heading turns at a bounded rate too, but no rule that
/// depends only on the present aim can promise that everywhere, and it can flip: in flight, when the aim passes over
/// the point it flew from on the ring (the target is then behind it); on the ring, while it turns, when the aim moves
/// to the far side of that point from the volley's `point`.
[[nodiscard]] SwordPose SwordAt(const Volley& volley, u32 sword, f64 tick, Vec2 aim);

/// Which of a volley's `targets` (not empty, as tagged at its cast) `sword` flies at, at `tick`. It starts at its own,
/// `sword` modulo their count. When the one it flies at dies at least the shape's `commitTicks` before the sword's
/// arrival, it moves on to one of the targets alive at that tick, chosen so the swords that move together spread evenly
/// over them; the others keep theirs. It repeats at each such death of the one it flies at. When the one it flies at
/// dies later than that, or none is alive at its death, it keeps the dead one and plunges into the ground at its frozen
/// position. So the target in force at the arrival tick is the one the sword strikes, and it never changes after.
///
/// At the arrival tick the logic hits that target if it was alive when the tick's arrivals resolved: arrivals resolve
/// before the tick's deaths, so a target dying on that tick still takes the hit (VolleyTarget::diedTick).
[[nodiscard]] u32 SwordTarget(const Volley& volley, u32 sword, u64 tick, std::span<const VolleyTarget> targets);

/// Where on its target `sword` (below the shape's count) aims, from the target's middle: a point within AIM_SCATTER,
/// drawn from the volley's seed, spread evenly over that disc. The same at every moment, so SwordAim() always adds it and
/// the aim never steps.
[[nodiscard]] Vec2 AimOffset(const Volley& volley, u32 sword);

/// The aim `sword` flies at, for SwordAt() at the same moment: the moment `RenderTick(simulationTick, alpha)`, with the
/// targets as `simulationTick` left them (the logic passes its tick and 1; the renderer the drawn snapshot's tick and the
/// frame's alpha, so the two can't disagree). It is the position of its target (SwordTarget()), between the target's
/// `previous` and `current` by `alpha`, moved by the sword's own AimOffset(), always, the frozen point of a dead one too. When
/// the sword moves on from a dead target, its aim eases from where it was then to the new target, over RETARGET_TICKS or the
/// way's length at RETARGET_SPEED, whichever is longer, but always ending by the sword's arrival: the aim, and with it the
/// sword, never jumps, and at the arrival tick it is exactly on its spot on the target it strikes. The commit window fits a
/// swing across the tagged area, so the swing moves the aim at up to RETARGET_PEAK, on top of the sword's own flight; only
/// targets that have since moved further apart than that area make it faster, to end by the arrival. RETARGET_PEAK can be
/// several times a slow sword's own flight speed (a short flight): such a sword visibly speeds up through the swing, though
/// smoothly.
[[nodiscard]] Vec2 SwordAim(
    const Volley& volley,
    u32 sword,
    u64 simulationTick,
    f32 alpha,
    std::span<const VolleyTarget> targets
);

} // namespace core
