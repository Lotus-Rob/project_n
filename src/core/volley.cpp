#include "core/volley.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/math.hpp"
#include "core/random.hpp"
#include "core/tick_clock.hpp"

namespace core {

namespace {

// The volley's own random numbers' purposes. They key on the volley's seed, not the game's, so they share no numbers with
// game/random_purposes.hpp's.
// How long a sword waits on its ring after its hold before it leaves (VolleyShape::departJitterTicks).
constexpr u32 PURPOSE_DEPART = 1;
// How far a sword's place round its ring strays from an even spacing (VolleyShape::angleJitter).
constexpr u32 PURPOSE_RING = 2;
// A sword's share of the sweep (VolleyShape::sweepJitter).
constexpr u32 PURPOSE_BEND = 3;
// Its draws put a target's index in the volley's targets where core::Random takes a stable ID: the order is fixed at
// the cast, so the index is stable for the volley's life. Never an entity's ID there.
constexpr u32 PURPOSE_SPREAD = 4;
// Where on its target a sword aims (AimOffset()): the way from the target's middle, then how far.
constexpr u32 PURPOSE_SCATTER_ANGLE = 5;
constexpr u32 PURPOSE_SCATTER_REACH = 6;
// How far in or out of its ring a sword's place lies (VolleyShape::scatter).
constexpr u32 PURPOSE_BAND = 7;
// Keyed on the wave, not a sword: how far round each ring's even spacing starts, so no two rings line up.
constexpr u32 PURPOSE_WAVE_TURN = 8;

// The bend of a short flight, as a fraction of its chord. Long flights approach the shape's sweep instead.
constexpr f64 SHORT_BEND = 0.5;
// Ticks a sword takes to turn from pointing out of its ring to facing its flight, as it sets off, its flight easing from
// rest: long enough that even half a turn stays a visible swing (at most 0.6 rad a tick). The trade-off for the look:
// the turn overlaps the flight's start, so by its end a sword has moved up to about 25 world units along its flight
// (about 1.5% of it), under about 7 while it still points more than 90° off the way it moves.
constexpr f64 TURN_TICKS = 8.0;
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

// A random number's value in [-1, 1): a jitter's share of its reach, either way.
f64 Either(u64 random) {
    const f64 centred = Centred(random);
    return centred + centred;
}

// What a sword's path depends on besides the time and the aim: drawn from the volley's seed, the same on every call.
struct SwordPlan {
    u64 launch = 0;          // it appears
    u64 departure = 0;       // it leaves its ring
    VolleyVector outwards{}; // unit
    f64 radius = 0.0;        // how far from the origin its place on its ring lies
    f64 bend = 1.0;          // this sword's share of the sweep
};

// A sword's wave and its place among that wave's swords.
struct WavePlace {
    u32 wave = 0;
    u32 index = 0;
    u32 size = 1;
};

// The first sword of `wave` (from 0 to the shape's `waves`, where it is the count): the swords before it share the count
// as the rings before it share the rings' radii, so every ring is as closely packed round its circumference. Rounding
// down each boundary sends each wave's leftover sword outwards. With no radius at all, every ring weighs the same. The
// boundaries never fall as the wave rises (each step of the share is correctly rounded), and at the last wave the share
// is the total over itself, exactly 1.
u32 WaveStart(const VolleyShape& shape, u32 wave) {
    const auto inner = static_cast<f64>(shape.ringRadius);
    const auto step = static_cast<f64>(shape.ringStep);
    // The radii of the first `rings` rings, summed: an arithmetic series.
    const auto radii = [inner, step](u32 rings) {
        const auto count = static_cast<f64>(rings);
        return (count * inner) + (step * count * (count - 1.0) * HALF);
    };
    const f64 total = radii(shape.waves);
    const f64 share = total > 0.0 ? radii(wave) / total : static_cast<f64>(wave) / static_cast<f64>(shape.waves);
    return static_cast<u32>(static_cast<f64>(shape.count) * share);
}

// The swords split over the waves in order of index, inner first, each wave's share of them its ring's share of the
// circumferences (WaveStart()). A wave takes a run of indices rather than every `waves`th one, so each still holds every
// target's swords in turn (SwordTarget() starts a sword at `sword` modulo the targets).
WavePlace PlaceInWave(const VolleyShape& shape, u32 sword) {
    PN_ASSERT(sword < shape.count, "a sword beyond its volley's count");
    PN_ASSERT(shape.waves >= 1, "a volley of no waves");
    // The last wave starting at or before `sword`: a binary search, as the boundaries never fall. A wave of no swords
    // starts where the next does, so the search passes over it.
    u32 low = 0;
    u32 high = shape.waves; // starts at the count, after every sword
    while (high - low > 1) {
        const u32 middle = low + ((high - low) / 2);
        (WaveStart(shape, middle) <= sword ? low : high) = middle;
    }
    const u32 first = WaveStart(shape, low);
    return { .wave = low, .index = sword - first, .size = WaveStart(shape, low + 1) - first };
}

u64 VolleyDraw(const Volley& volley, u32 sword, u32 purpose) {
    PN_ASSERT(sword < volley.shape.count, "a sword beyond its volley's count");
    return Random(volley.seed, 0, 0, purpose, sword);
}

// A wave's swords appear in order of index, which is clockwise round the ring (the even spacing's angle rises with it, and
// world y grows downwards), so the ring draws itself in one sweep over the stagger.
u64 Launch(const Volley& volley, WavePlace place) {
    const u64 waveStart = volley.startTick + (static_cast<u64>(place.wave) * volley.shape.waveGapTicks);
    return waveStart + ((u64{ volley.shape.staggerTicks } * place.index) / place.size);
}

u64 Departure(const Volley& volley, u32 sword, u64 launch) {
    const VolleyShape& shape = volley.shape;
    return launch + shape.outTicks + shape.holdTicks +
           ToBelow(VolleyDraw(volley, sword, PURPOSE_DEPART), shape.departJitterTicks);
}

SwordPlan Plan(const Volley& volley, u32 sword) {
    const VolleyShape& shape = volley.shape;
    const WavePlace place = PlaceInWave(shape, sword);
    const f64 waveTurn = ToUnit(Random(volley.seed, 0, 0, PURPOSE_WAVE_TURN, place.wave));
    const f64 even = FULL_TURN * (static_cast<f64>(place.index) + waveTurn) / static_cast<f64>(place.size);
    const f64 angle = even + (static_cast<f64>(shape.angleJitter) * Either(VolleyDraw(volley, sword, PURPOSE_RING)));
    const f64 ring = static_cast<f64>(shape.ringRadius) + (static_cast<f64>(place.wave) * static_cast<f64>(shape.ringStep));
    const f64 band = static_cast<f64>(shape.scatter) * Either(VolleyDraw(volley, sword, PURPOSE_BAND));
    const u64 launch = Launch(volley, place);
    return {
        .launch = launch,
        .departure = Departure(volley, sword, launch),
        .outwards = { .x = math::Cos(angle), .y = math::Sin(angle) },
        .radius = ring * (1.0 + band),
        .bend = 1.0 + (static_cast<f64>(shape.sweepJitter) * Either(VolleyDraw(volley, sword, PURPOSE_BEND))),
    };
}

// A sword's chain of targets: it starts at its own, `sword` modulo their count. When the one it flies at dies at least
// the volley's commit window before the sword's `arrival`, it moves on to one of those alive at that tick; otherwise, or
// when none is alive, the chain ends there. Each move is to a target that outlives the last, so the chain ends within
// `count` links.
//
// The pick spreads the swords that move together evenly: they share their chain so far, and their spread indices (the
// mixed-radix digits of `sword / count`, one used per move) count up through the group, so `index mod living` deals
// them out in turn. An offset drawn from the target left and the move number starts each group's deal at a different
// living target, so many small groups (many targets, few swords each, whose digits run out) don't all pile onto the
// first living one. Calls `moved(tick, from)` at each move up to `time`, in order, and returns the target in force then.
template<typename Moved>
u32 WalkTargets(
    const Volley& volley,
    u32 sword,
    u64 arrival,
    f64 time,
    std::span<const VolleyTarget> targets,
    const Moved& moved
) {
    PN_ASSERT(!targets.empty(), "a volley with no targets");
    PN_ASSERT(volley.shape.commitTicks >= MIN_COMMIT_TICKS, "a volley whose cast left its commit window unresolved");
    const std::size_t count = targets.size();
    std::size_t link = sword % count;
    std::size_t spread = sword / count;
    for (u32 move = 0; move < count; ++move) {
        const VolleyTarget& current = targets[link];
        if (current.AliveAt(time) || current.diedTick + volley.shape.commitTicks > arrival) {
            break; // alive, or dead too close to the arrival: it plunges where this one died
        }
        const auto died = static_cast<f64>(current.diedTick);
        const auto living = static_cast<std::size_t>(std::ranges::count_if(targets, [died](const VolleyTarget& target) {
            return target.AliveAt(died);
        }));
        if (living == 0) {
            break; // it plunges where this one died
        }
        const u32 offset = ToBelow(Random(volley.seed, 0, link, PURPOSE_SPREAD, move), static_cast<u32>(living));
        std::size_t skip = (spread + offset) % living;
        spread /= living;
        std::size_t next = 0;
        for (; next < count; ++next) {
            if (targets[next].AliveAt(died)) {
                if (skip == 0) {
                    break;
                }
                --skip;
            }
        }
        moved(died, link);
        link = next;
    }
    return static_cast<u32>(link);
}

VolleyVector Interpolated(const VolleyTarget& target, f32 alpha) {
    const VolleyVector previous = Widen(target.previous);
    return previous + ((Widen(target.current) - previous) * static_cast<f64>(alpha));
}

// Where a swing that started at `swingStart` from `from` towards `towards` is at `time`: exactly `towards` once it's over,
// which is by `arrival` at the latest (a move needs the volley's commit window before it, so the swing has time, and at
// RETARGET_SPEED when the targets are still within the tagged area). Its length follows the way to `towards`, so a
// target that moves while the swing is under way changes it smoothly; its progress keeps rising while that target moves
// slower than RETARGET_SPEED.
VolleyVector Swing(VolleyVector from, VolleyVector towards, f64 swingStart, f64 time, u64 arrival) {
    const f64 ticks =
        std::min(std::max(RETARGET_TICKS, Length(towards - from) / RETARGET_SPEED), static_cast<f64>(arrival) - swingStart);
    const f64 progress = (time - swingStart) / ticks;
    return progress >= 1.0 ? towards : Lerp(from, towards, Ease(progress, 0.0, 0.0));
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
VolleyVector Leaving(VolleyVector ring, VolleyVector aim, f64 sweep, f64 share) {
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

u32 WaveOf(const Volley& volley, u32 sword) {
    return PlaceInWave(volley.shape, sword).wave;
}

u64 LaunchTick(const Volley& volley, u32 sword) {
    return Launch(volley, PlaceInWave(volley.shape, sword));
}

u64 DepartureTick(const Volley& volley, u32 sword) {
    return Departure(volley, sword, LaunchTick(volley, sword));
}

u64 ArrivalTick(const Volley& volley, u32 sword) {
    return DepartureTick(volley, sword) + volley.shape.flightTicks;
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
    const f64 radius = plan.radius;
    const f64 sweep = static_cast<f64>(shape.sweep);
    const f64 flightTicks = static_cast<f64>(shape.flightTicks);
    const VolleyVector target = Widen(aim);
    const VolleyVector ring = Widen(volley.origin) + (plan.outwards * radius);
    // The way its flight leaves the ring, planned from the cast's fixed point and then corrected to the moving aim, so
    // its turn to it never flips side as the aim moves. Only a turn, the bolt and a flight of no length need it.
    struct Ways {
        VolleyVector planned{};
        VolleyVector leaving{};
    };
    const auto ways = [&]() -> Ways {
        const VolleyVector planned = Unit(Leaving(ring, Widen(volley.point), sweep, plan.bend), plan.outwards);
        return { .planned = planned, .leaving = Unit(Leaving(ring, target, sweep, plan.bend), planned) };
    };

    SwordPose pose{};
    pose.position = Narrow(ring);
    // On its ring it points straight out of it; with no ring (the bolt), the way it will fly.
    pose.heading = Narrow(radius > 0.0 ? plan.outwards : ways().leaving);
    const f64 elapsed = tick - static_cast<f64>(plan.launch);
    if (elapsed < 0.0) {
        return pose;
    }
    const f64 outTicks = static_cast<f64>(shape.outTicks);
    pose.shown = elapsed < outTicks ? static_cast<f32>(elapsed / outTicks) : 1.0F;
    const f64 flown = tick - static_cast<f64>(plan.departure);
    if (flown < 0.0) {
        return pose;
    }
    // As it sets off it turns from pointing out of its ring to facing the way it moves, as angles, never wrapped. A flight
    // shorter than the turn turns over the whole flight, so it faces its flight by its arrival.
    const f64 turnTicks = std::min(TURN_TICKS, flightTicks);
    const auto heading = [&](VolleyVector tangent, f64 since) {
        const f64 turned = radius > 0.0 && turnTicks > 0.0 ? Ease(since / turnTicks, 0.0, 0.0) : 1.0;
        if (turned >= 1.0 && Length(tangent) > 0.0) {
            return Narrow(Unit(tangent, {}));
        }
        const Ways way = ways();
        const VolleyVector moving = Unit(tangent, way.leaving);
        if (turned >= 1.0) {
            return Narrow(moving);
        }
        const f64 flight = AngleBetween(plan.outwards, way.planned) + AngleBetween(way.planned, way.leaving) +
                           AngleBetween(way.leaving, moving);
        return Narrow(Rotate(plan.outwards, turned * flight));
    };
    const SwordFlight flight = FlightTo(ring, target, sweep, plan.bend);
    if (flown < flightTicks) {
        const FlightPoint point = Along(flight, Ease(flown / flightTicks, 0.0, ARRIVAL_SPEED));
        pose.position = Narrow(point.position);
        pose.heading = heading(point.tangent, flown);
        return pose;
    }
    pose.position = aim;
    pose.heading = heading(flight.end - flight.second, flown);
    const f64 after = flown - flightTicks;
    const f64 fadeTicks = static_cast<f64>(shape.fadeTicks);
    if (after > 0.0) {
        pose.shown = fadeTicks > 0.0 ? static_cast<f32>(std::max(0.0, 1.0 - (after / fadeTicks))) : 0.0F;
    }
    return pose;
}

Vec2 AimOffset(const Volley& volley, u32 sword) {
    const f64 angle = FULL_TURN * ToUnit(VolleyDraw(volley, sword, PURPOSE_SCATTER_ANGLE));
    // The square root spreads the spots evenly over the disc rather than crowding its middle.
    const f64 reach = static_cast<f64>(AIM_SCATTER) * std::sqrt(ToUnit(VolleyDraw(volley, sword, PURPOSE_SCATTER_REACH)));
    return { .x = static_cast<f32>(reach * math::Cos(angle)), .y = static_cast<f32>(reach * math::Sin(angle)) };
}

u32 SwordTarget(const Volley& volley, u32 sword, u64 tick, std::span<const VolleyTarget> targets) {
    return WalkTargets(volley, sword, ArrivalTick(volley, sword), static_cast<f64>(tick), targets, [](f64, std::size_t) {});
}

Vec2 SwordAim(const Volley& volley, u32 sword, u64 simulationTick, f32 alpha, std::span<const VolleyTarget> targets) {
    const f64 time = RenderTick(simulationTick, static_cast<f64>(alpha));
    const u64 arrival = ArrivalTick(volley, sword);
    // The aim when the latest move began: where the swing before it had reached, from the frozen point of the target it
    // left. Every target it left is dead and frozen, so this needs no history.
    bool swinging = false;
    VolleyVector swingFrom{};
    f64 swingStart = 0.0;
    const u32 target = WalkTargets(volley, sword, arrival, time, targets, [&](f64 died, std::size_t from) {
        const VolleyVector left = Widen(targets[from].current);
        swingFrom = swinging ? Swing(swingFrom, left, swingStart, died, arrival) : left;
        swingStart = died;
        swinging = true;
    });
    const VolleyVector towards = Interpolated(targets[target], alpha);
    return Narrow(swinging ? Swing(swingFrom, towards, swingStart, time, arrival) : towards) + AimOffset(volley, sword);
}

} // namespace core
