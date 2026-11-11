#include "core/volley.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/math.hpp"
#include "core/random.hpp"

namespace core {

namespace {

// The volley's own random numbers' purposes. They key on the volley's seed, not the game's, so they share no numbers with
// game/random_purposes.hpp's.
constexpr u32 PURPOSE_LAUNCH = 1;
constexpr u32 PURPOSE_RING = 2;
constexpr u32 PURPOSE_BEND = 3;

// How far a sword's place on the ring may stray from an even spacing, in spacings from one side to the other: the ring
// looks hand-thrown, and its widest gap stays under one and a half spacings.
constexpr f64 RING_SPREAD = 0.5;
// How much a sword's bend may differ from the rest's, as a fraction from least to most, so 1,000 swords fly as a swarm
// rather than one family of nested curves.
constexpr f64 BEND_SPREAD = 0.4;
// The bend of a short flight, as a fraction of its chord. Long flights approach the shape's sweep instead.
constexpr f64 SHORT_BEND = 0.5;
// The fraction of the out phase after which a sword starts turning from pointing outwards to facing its flight.
constexpr f64 TURN_START = 0.5;
// A sword's speed as it leaves the caster, as a multiple of its average speed out to the ring: a burst.
constexpr f64 BURST_SPEED = 1.5;
// A sword's speed at its arrival, as a multiple of its average speed in flight: it still strikes rather than settles.
constexpr f64 ARRIVAL_SPEED = 0.5;
// A cubic Bézier whose inner control points sit the same distance to one side of its chord bends 3/4 of that distance
// at its middle; this offset makes the bend exactly the wanted one.
constexpr f64 SIDE_ARM = 4.0 / 3.0;
// The inner control points sit a third of the way along the chord from each end, so a flight with no bend is paced evenly.
constexpr f64 THIRD = 1.0 / 3.0;
// The middle of a random number's range [0, 1).
constexpr f64 HALF = 0.5;
// The ring's angles, in radians.
constexpr f64 FULL_TURN = 2.0 * std::numbers::pi;
// A cubic Hermite curve's coefficients of u² and u³ for a move from rest to rest: 3u² − 2u³.
constexpr f64 HERMITE_SQUARE = 3.0;
constexpr f64 HERMITE_CUBE = 2.0;

// A position or direction in f64: the path is computed in f64 and rounded to f32 once.
struct VolleyVector {
    f64 x = 0.0;
    f64 y = 0.0;

    friend VolleyVector operator+(VolleyVector lhs, VolleyVector rhs) {
        return { .x = lhs.x + rhs.x, .y = lhs.y + rhs.y };
    }
    friend VolleyVector operator-(VolleyVector lhs, VolleyVector rhs) {
        return { .x = lhs.x - rhs.x, .y = lhs.y - rhs.y };
    }
    friend VolleyVector operator*(VolleyVector vec, f64 factor) {
        return { .x = vec.x * factor, .y = vec.y * factor };
    }
};

VolleyVector Widen(Vec2 vec) {
    return { .x = static_cast<f64>(vec.x), .y = static_cast<f64>(vec.y) };
}

Vec2 Narrow(VolleyVector vec) {
    return { .x = static_cast<f32>(vec.x), .y = static_cast<f32>(vec.y) };
}

f64 Dot(VolleyVector lhs, VolleyVector rhs) {
    return (lhs.x * rhs.x) + (lhs.y * rhs.y);
}

f64 Cross(VolleyVector lhs, VolleyVector rhs) {
    return (lhs.x * rhs.y) - (lhs.y * rhs.x);
}

f64 Length(VolleyVector vec) {
    return std::sqrt(Dot(vec, vec));
}

// `vec` turned a quarter turn, the same way for every sword.
VolleyVector Sideways(VolleyVector vec) {
    return { .x = -vec.y, .y = vec.x };
}

VolleyVector Rotate(VolleyVector vec, f64 radians) {
    const f64 cosine = math::Cos(radians);
    const f64 sine = math::Sin(radians);
    return { .x = (cosine * vec.x) - (sine * vec.y), .y = (sine * vec.x) + (cosine * vec.y) };
}

// The signed angle that turns `start` to `finish`, from -π to π.
f64 AngleBetween(VolleyVector start, VolleyVector finish) {
    return math::Atan2(Cross(start, finish), Dot(start, finish));
}

// `vec` scaled to unit length, or `fallback` when it has none.
VolleyVector Unit(VolleyVector vec, VolleyVector fallback) {
    const f64 length = Length(vec);
    return length > 0.0 ? vec * (1.0 / length) : fallback;
}

VolleyVector Lerp(VolleyVector from, VolleyVector towards, f64 progress) {
    return from + ((towards - from) * progress);
}

// An easing from 0 to 1 as `progress` goes from 0 to 1 (clamped), starting and ending at the given speeds as multiples of
// the average: a cubic Hermite curve, so it never jumps and its speed never does either.
f64 Ease(f64 progress, f64 startSpeed, f64 endSpeed) {
    const f64 time = std::clamp(progress, 0.0, 1.0);
    const f64 square = HERMITE_SQUARE - startSpeed - startSpeed - endSpeed;
    const f64 cube = startSpeed + endSpeed - HERMITE_CUBE;
    return time * (startSpeed + (time * (square + (time * cube))));
}

// A random number's value in [-1/2, 1/2).
f64 Centred(u64 random) {
    return ToUnit(random) - HALF;
}

// What a sword's path depends on besides the time and the aim: drawn from the volley's seed, the same on every call.
struct SwordPlan {
    u64 launch = 0;
    VolleyVector outwards{}; // unit
    f64 bend = 1.0;          // this sword's share of the sweep
};

SwordPlan Plan(const Volley& volley, u32 sword) {
    PN_ASSERT(sword < volley.shape.count, "a sword beyond its volley's count");
    const auto draw = [&volley, sword](u32 purpose) { return Random(volley.seed, 0, 0, purpose, sword); };
    const f64 place = static_cast<f64>(sword) + (RING_SPREAD * Centred(draw(PURPOSE_RING)));
    const f64 angle = FULL_TURN * place / static_cast<f64>(volley.shape.count);
    return {
        .launch = volley.startTick + ToBelow(draw(PURPOSE_LAUNCH), volley.shape.staggerTicks),
        .outwards = { .x = math::Cos(angle), .y = math::Sin(angle) },
        .bend = 1.0 + (BEND_SPREAD * Centred(draw(PURPOSE_BEND))),
    };
}

// The flight from the ring to the aim: a cubic Bézier whose inner control points sit a third of the way along the chord
// and equally far to the same side of it, so it bends like an arc, the same way for every sword, with the chord's
// direction never reversed (no cusps, so no sudden turns). The bend, as a fraction of the chord, is SHORT_BEND on short
// flights and falls so that a long flight bends by nearly the shape's sweep. Everything follows the chord smoothly, so
// the curve follows a moving aim, and with no sweep it is the straight chord, evenly paced.
struct SwordFlight {
    VolleyVector start{};
    VolleyVector first{};
    VolleyVector second{};
    VolleyVector end{};
};

// A point on a flight and the way it moves there (not unit length).
struct FlightPoint {
    VolleyVector position{};
    VolleyVector tangent{};
};

// The control points' offset from the chord's thirds, for a chord `chord`.
VolleyVector Bend(VolleyVector chord, f64 sweep, f64 share) {
    if (sweep <= 0.0) {
        return {};
    }
    const f64 shortest = sweep / SHORT_BEND;
    const f64 ratio = sweep * share / std::sqrt(Dot(chord, chord) + (shortest * shortest));
    return Sideways(chord) * (SIDE_ARM * ratio);
}

// The way a flight from `ring` towards `aim` leaves the ring (not unit length).
VolleyVector Departure(VolleyVector ring, VolleyVector aim, f64 sweep, f64 share) {
    const VolleyVector chord = aim - ring;
    return (chord * THIRD) + Bend(chord, sweep, share);
}

SwordFlight FlightTo(VolleyVector ring, VolleyVector aim, f64 sweep, f64 share) {
    const VolleyVector chord = aim - ring;
    const VolleyVector third = chord * THIRD;
    const VolleyVector side = Bend(chord, sweep, share);
    return { .start = ring, .first = ring + third + side, .second = aim - third + side, .end = aim };
}

// De Casteljau's construction: its last segment is the tangent, and progress 0 gives the start exactly.
FlightPoint Along(const SwordFlight& flight, f64 progress) {
    const VolleyVector startSide = Lerp(flight.start, flight.first, progress);
    const VolleyVector middle = Lerp(flight.first, flight.second, progress);
    const VolleyVector endSide = Lerp(flight.second, flight.end, progress);
    const VolleyVector from = Lerp(startSide, middle, progress);
    const VolleyVector towards = Lerp(middle, endSide, progress);
    return { .position = Lerp(from, towards, progress), .tangent = towards - from };
}

} // namespace

u64 LaunchTick(const Volley& volley, u32 sword) {
    return Plan(volley, sword).launch;
}

u64 ArrivalTick(const Volley& volley, u32 sword) {
    return LaunchTick(volley, sword) + volley.shape.outTicks + volley.shape.flightTicks;
}

u64 LastArrival(const Volley& volley) {
    u64 last = volley.startTick;
    for (u32 sword = 0; sword < volley.shape.count; ++sword) {
        last = std::max(last, ArrivalTick(volley, sword));
    }
    return last;
}

SwordPose SwordAt(const Volley& volley, u32 sword, f64 tick, Vec2 aim) {
    const VolleyShape& shape = volley.shape;
    const SwordPlan plan = Plan(volley, sword);
    const f64 radius = static_cast<f64>(shape.ringRadius);
    const f64 sweep = static_cast<f64>(shape.sweep);
    const f64 outTicks = static_cast<f64>(shape.outTicks);
    const f64 flightTicks = static_cast<f64>(shape.flightTicks);
    const VolleyVector origin = Widen(volley.origin);
    const VolleyVector target = Widen(aim);
    const VolleyVector ring = origin + (plan.outwards * radius);
    const f64 elapsed = tick - static_cast<f64>(plan.launch);

    // Where the sword faces while it can't face where it moves: on the spot at the origin, or at rest on the ring. Planned
    // from the cast's fixed point, then corrected to the moving aim, so its turn never flips side as the aim moves.
    const auto facing = [&](f64 turned) -> VolleyVector {
        if (radius > 0.0 && turned <= 0.0) {
            return plan.outwards;
        }
        const VolleyVector planned = Unit(Departure(ring, Widen(volley.point), sweep, plan.bend), plan.outwards);
        const VolleyVector actual = Unit(Departure(ring, target, sweep, plan.bend), planned);
        // With no ring to burst out to, it faces its flight from the start (the bolt).
        return radius > 0.0
                   ? Rotate(plan.outwards, turned * (AngleBetween(plan.outwards, planned) + AngleBetween(planned, actual)))
                   : actual;
    };

    SwordPose pose{};
    if (elapsed < 0.0) {
        pose.position = volley.origin;
        pose.heading = Narrow(facing(0.0));
        return pose;
    }
    pose.shown = 1.0F;
    if (elapsed < outTicks) {
        const f64 progress = elapsed / outTicks;
        pose.position = Narrow(origin + (plan.outwards * (radius * Ease(progress, BURST_SPEED, 0.0))));
        pose.heading = Narrow(facing(Ease((progress - TURN_START) / (1.0 - TURN_START), 0.0, 0.0)));
        return pose;
    }
    // Only a flight of no length has no tangent; it faces as it did on the ring.
    const auto heading = [&facing](VolleyVector tangent) {
        return Narrow(Length(tangent) > 0.0 ? Unit(tangent, {}) : facing(1.0));
    };
    const SwordFlight flight = FlightTo(ring, target, sweep, plan.bend);
    const f64 flown = elapsed - outTicks;
    if (flown < flightTicks) {
        const FlightPoint point = Along(flight, Ease(flown / flightTicks, 0.0, ARRIVAL_SPEED));
        pose.position = Narrow(point.position);
        pose.heading = heading(point.tangent);
        return pose;
    }
    pose.position = aim;
    pose.heading = heading(flight.end - flight.second);
    const f64 after = flown - flightTicks;
    const f64 fadeTicks = static_cast<f64>(shape.fadeTicks);
    if (after > 0.0) {
        pose.shown = fadeTicks > 0.0 ? static_cast<f32>(std::max(0.0, 1.0 - (after / fadeTicks))) : 0.0F;
    }
    return pose;
}

u32 SwordTarget(u32 sword, std::span<const VolleyTarget> targets) {
    PN_ASSERT(!targets.empty(), "a volley with no targets");
    const std::size_t count = targets.size();
    const std::size_t own = sword % count;
    if (targets[own].alive) {
        return static_cast<u32>(own);
    }
    const auto living = static_cast<std::size_t>(std::ranges::count_if(targets, &VolleyTarget::alive));
    if (living == 0) {
        return static_cast<u32>(own);
    }
    std::size_t skip = (sword / count) % living;
    for (std::size_t index = 0; index < count; ++index) {
        if (targets[index].alive) {
            if (skip == 0) {
                return static_cast<u32>(index);
            }
            --skip;
        }
    }
    return static_cast<u32>(own); // unreachable: `skip` is below the living count
}

} // namespace core
