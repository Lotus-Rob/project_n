#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/math.hpp"
#include "core/volley.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 START = 600;
constexpr u64 SEED = 0x5eed'0123'4567'89abULL;
constexpr core::Vec2 ORIGIN{ .x = 100.0F, .y = 50.0F };
constexpr core::Vec2 POINT{ .x = 700.0F, .y = -100.0F };
constexpr f32 SWORDS_RADIUS = 260.0F; // the shipped swords' tag radius
// The shipped swords (assets/data/abilities.yaml) in ticks, as their cast resolves them.
constexpr core::VolleyShape SWORDS{
    .count = 1'000,
    .waves = 5,
    .ringRadius = 160.0F,
    .ringStep = 100.0F,
    .scatter = 0.0F,
    .angleJitter = 0.0F,
    .staggerTicks = 6,
    .waveGapTicks = 12,
    .outTicks = 21,
    .holdTicks = 18,
    .departJitterTicks = 15,
    .flightTicks = 102,
    .sweep = 420.0F,
    .sweepJitter = 0.6F,
    .fadeTicks = 18,
    .commitTicks = core::CommitTicksFor(SWORDS_RADIUS),
};
constexpr core::Volley VOLLEY{ .use = 7, .startTick = START, .seed = SEED, .origin = ORIGIN, .point = POINT, .shape = SWORDS };
// The swords with loose rings, as a shape may still set them: places strayed in and out by up to a quarter of the radius
// and round their even spacing by up to half a radian.
constexpr f32 LOOSE_SCATTER = 0.25F;
constexpr f32 LOOSE_ANGLE_JITTER = 0.5F;
constexpr core::Volley LOOSE = [] {
    core::Volley loose = VOLLEY;
    loose.shape.scatter = LOOSE_SCATTER;
    loose.shape.angleJitter = LOOSE_ANGLE_JITTER;
    return loose;
}();
constexpr u64 SUBSTEPS = 8; // render times a tick
constexpr f64 SUBSTEP = 1.0 / static_cast<f64>(SUBSTEPS);
constexpr f64 UNIT_SLACK = 1e-5; // of a heading's length
constexpr f64 FULL_TURN = 2.0 * std::numbers::pi;
constexpr std::array<u32, 4> SAMPLED_SWORDS{ { 0, 1, 499, 999 } };

f64 Distance(core::Vec2 from, core::Vec2 towards) {
    const f64 deltaX = static_cast<f64>(towards.x) - static_cast<f64>(from.x);
    const f64 deltaY = static_cast<f64>(towards.y) - static_cast<f64>(from.y);
    return std::sqrt((deltaX * deltaX) + (deltaY * deltaY));
}

f64 Cross(core::Vec2 lhs, core::Vec2 rhs) {
    return (static_cast<f64>(lhs.x) * static_cast<f64>(rhs.y)) - (static_cast<f64>(lhs.y) * static_cast<f64>(rhs.x));
}

f64 Dot(core::Vec2 lhs, core::Vec2 rhs) {
    return (static_cast<f64>(lhs.x) * static_cast<f64>(rhs.x)) + (static_cast<f64>(lhs.y) * static_cast<f64>(rhs.y));
}

// The angle between two headings, in radians, from 0 to π.
f64 Turn(core::Vec2 from, core::Vec2 towards) {
    return std::abs(core::math::Atan2(Cross(from, towards), Dot(from, towards)));
}

// The distance of `position` from the line through `from` and `towards`.
f64 OffLine(core::Vec2 position, core::Vec2 from, core::Vec2 towards) {
    return std::abs(Cross(towards - from, position - from)) / Distance(from, towards);
}

bool IsUnit(core::Vec2 heading) {
    return std::abs(Distance({}, heading) - 1.0) < UNIT_SLACK;
}

core::Vec2 MovingAim(f64 tick) {
    constexpr f64 SPEED = 3.0; // world units a tick, across the flight
    return { .x = POINT.x, .y = static_cast<f32>(static_cast<f64>(POINT.y) + (SPEED * (tick - static_cast<f64>(START)))) };
}

core::Vec2 StillAim(f64 /*tick*/) {
    return POINT;
}

// No step longer than this many times the median step of its phase: the flight or the fade.
constexpr f64 MOST_STEP = 3.0;
// No heading turns more than this between samples, in radians.
constexpr f64 MOST_TURN = 0.25;

// The tick `sword` leaves its ring: its flight's start, `flightTicks` before its arrival.
u64 DepartureTick(const core::Volley& volley, u32 sword) {
    return core::ArrivalTick(volley, sword) - volley.shape.flightTicks;
}

// A retarget swing a path is checked through: a retarget may move a slow sword at the designed swing pace, never faster.
// While the swing is under way (render times `from` to `until`), a step may reach `peak`, the swing's designed fastest
// step, on top of the sword's own median step, where MOST_STEP times that median alone would take a slow sword's swing
// for a jump. With no swing (the default), every step keeps the plain bound.
struct SwingBound {
    f64 from = 0.0;
    f64 until = 0.0;
    f64 peak = 0.0; // world units a sample
};

// One step between samples, and whether a retarget swing was under way during it.
struct Step {
    f64 length = 0.0;
    bool swinging = false;
};

// Whether no step is longer than MOST_STEP times the median or, during a swing, than the larger of that and the median
// plus `swingPeak` (SwingBound).
bool NoJump(const std::vector<Step>& steps, f64 swingPeak) {
    if (steps.empty()) {
        return true;
    }
    std::vector<f64> lengths(steps.size());
    std::ranges::transform(steps, lengths.begin(), &Step::length);
    std::ranges::nth_element(lengths, lengths.begin() + static_cast<std::ptrdiff_t>(lengths.size() / 2));
    const f64 median = lengths[lengths.size() / 2];
    const f64 plain = MOST_STEP * median;
    const f64 swing = std::max(plain, median + swingPeak);
    return std::ranges::all_of(steps, [plain, swing](const Step& step) {
        return step.length <= (step.swinging ? swing : plain);
    });
}

// Checks one sword's path from its appearance, through its hold on the ring, its turn, its flight and its fade, sampled
// between ticks, flying at `aimAt(tick)`, through the retarget swing `swing` if any: on the ring it never moves; in flight
// and after, no step is a jump; and its heading never turns sharply. Each check asserts its worst value once.
template<typename AimAt>
void CheckSmooth(const core::Volley& volley, u32 sword, const AimAt& aimAt, SwingBound swing = {}) {
    const u64 launch = core::LaunchTick(volley, sword);
    const u64 departure = DepartureTick(volley, sword);
    const u64 arrival = core::ArrivalTick(volley, sword);
    const u64 samples = (arrival + volley.shape.fadeTicks - launch) * SUBSTEPS;
    std::array<std::vector<Step>, 2> steps{}; // flight, fade
    core::SwordPose last = core::SwordAt(volley, sword, static_cast<f64>(launch), aimAt(static_cast<f64>(launch)));
    bool wellFormed = true; // every position finite, every heading unit length
    f64 mostOnRing = 0.0;   // the longest step before its departure
    f64 mostTurn = 0.0;
    for (u64 sample = 1; sample <= samples; ++sample) {
        const f64 tick = static_cast<f64>(launch) + (static_cast<f64>(sample) * SUBSTEP);
        const core::SwordPose pose = core::SwordAt(volley, sword, tick, aimAt(tick));
        wellFormed = wellFormed && core::IsFinite(pose.position) && IsUnit(pose.heading);
        mostTurn = std::max(mostTurn, Turn(last.heading, pose.heading));
        const f64 length = Distance(last.position, pose.position);
        if (tick <= static_cast<f64>(departure)) {
            mostOnRing = std::max(mostOnRing, length);
        }
        else {
            const bool swinging = tick > swing.from && tick - SUBSTEP < swing.until;
            steps.at(tick <= static_cast<f64>(arrival) ? 0 : 1).push_back({ .length = length, .swinging = swinging });
        }
        last = pose;
    }
    CHECK(wellFormed);
    CHECK(mostOnRing == 0.0);
    CHECK(mostTurn < MOST_TURN);
    REQUIRE_FALSE(steps[0].empty());
    const bool flightSmooth = NoJump(steps[0], swing.peak);
    const bool fadeSmooth = NoJump(steps[1], swing.peak);
    CHECK(flightSmooth);
    CHECK(fadeSmooth);
}

// Four tagged targets alive round the point, at the corners of a square.
std::array<core::VolleyTarget, 4> FourTargets() {
    constexpr f32 HALF_SIDE = 90.0F;
    constexpr std::array<core::Vec2, 4> CORNERS{ {
        { .x = -HALF_SIDE, .y = -HALF_SIDE },
        { .x = HALF_SIDE, .y = -HALF_SIDE },
        { .x = HALF_SIDE, .y = HALF_SIDE },
        { .x = -HALF_SIDE, .y = HALF_SIDE },
    } };
    std::array<core::VolleyTarget, 4> targets{};
    for (std::size_t index = 0; index < targets.size(); ++index) {
        const core::Vec2 corner = POINT + CORNERS.at(index);
        targets.at(index) = { .entity = core::StableId{ index + 1 }, .previous = corner, .current = corner };
    }
    return targets;
}

std::array<u32, 4> CountTargets(u64 tick, std::span<const core::VolleyTarget> targets) {
    std::array<u32, 4> counts{};
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        const u32 target = core::SwordTarget(VOLLEY, sword, tick, targets);
        REQUIRE(target < targets.size());
        ++counts.at(target);
    }
    return counts;
}

// Where `sword` aims on a target whose middle is at `middle`: its own spot (core::AimOffset()).
core::Vec2 Spot(core::Vec2 middle, u32 sword) {
    return middle + core::AimOffset(VOLLEY, sword);
}

// The aim at the render time `time`: the snapshot it leads to and the alpha that reaches it, as the renderer passes them.
core::Vec2 AimAt(u32 sword, f64 time, std::span<const core::VolleyTarget> targets) {
    const f64 snapshot = std::ceil(time);
    return core::SwordAim(VOLLEY, sword, static_cast<u64>(snapshot), static_cast<f32>(time - snapshot + 1.0), targets);
}

// Every first-wave sword in flight (they leave their ring from 639 to 658), and more than the commit window (33) before
// the earliest arrival (about 741), so every sword can move on.
constexpr u64 FIRST_DEATH = 680;
// Within the commit window of the first wave's earliest arrivals, so those plunge, and well before the rest.
constexpr u64 SECOND_DEATH = 715;

// The radius of `sword`'s ring: the first ring's, then a ring step further for each wave before its own.
f64 RingOf(const core::Volley& volley, u32 sword) {
    return static_cast<f64>(volley.shape.ringRadius) +
           (static_cast<f64>(core::WaveOf(volley, sword)) * static_cast<f64>(volley.shape.ringStep));
}

// Where `sword` stands on its ring, from its appearance until it leaves.
core::Vec2 RingPoint(const core::Volley& volley, u32 sword) {
    return core::SwordAt(volley, sword, static_cast<f64>(DepartureTick(volley, sword)), POINT).position;
}

// The angle of `out` round the origin, in radians from -π to π. World y grows downwards, so a rising angle turns
// clockwise on the screen.
f64 AngleOf(core::Vec2 out) {
    return core::math::Atan2(static_cast<f64>(out.y), static_cast<f64>(out.x));
}

// How far clockwise `angle` lies from `from`, in radians from 0 to below a full turn.
f64 ClockwiseFrom(f64 from, f64 angle) {
    const f64 turned = std::fmod(angle - from, FULL_TURN);
    return turned < 0.0 ? turned + FULL_TURN : turned;
}

f64 Mean(std::span<const f64> values) {
    return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<f64>(values.size());
}

f64 StandardDeviation(std::span<const f64> values) {
    const f64 mean = Mean(values);
    const f64 squares = std::accumulate(values.begin(), values.end(), 0.0, [mean](f64 sum, f64 value) {
        return sum + ((value - mean) * (value - mean));
    });
    return std::sqrt(squares / static_cast<f64>(values.size()));
}

TEST_CASE("a wave's ring forms before the next") {
    std::array<u32, SWORDS.waves> sizes{};
    std::array<u64, SWORDS.waves> earliest{};
    std::array<u64, SWORDS.waves> latest{};
    earliest.fill(std::numeric_limits<u64>::max());
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        const u32 wave = core::WaveOf(VOLLEY, sword);
        REQUIRE(wave < SWORDS.waves);
        ++sizes.at(wave);
        earliest.at(wave) = std::min(earliest.at(wave), core::LaunchTick(VOLLEY, sword));
        latest.at(wave) = std::max(latest.at(wave), core::LaunchTick(VOLLEY, sword));
    }
    // As even as can be.
    CHECK(std::ranges::max(sizes) - std::ranges::min(sizes) <= 1);
    // Each wave's first sword appears a wave gap after the one before's, the first at the cast, and the wave's last
    // within its stagger.
    u64 worstStart = 0;
    u64 worstSpread = 0;
    for (std::size_t wave = 0; wave < SWORDS.waves; ++wave) {
        const u64 start = START + (wave * SWORDS.waveGapTicks);
        worstStart = std::max(worstStart, std::max(earliest.at(wave), start) - std::min(earliest.at(wave), start));
        worstSpread = std::max(worstSpread, latest.at(wave) - earliest.at(wave));
    }
    CHECK(worstStart == 0);
    CHECK(worstSpread == SWORDS.staggerTicks - 1);
}

TEST_CASE("a sword appears at its ring place and holds still") {
    // The first wave's first and last, the second's first, one of the middle wave's, and the volley's last.
    constexpr std::array<u32, 5> SAMPLED{ { 0, 199, 200, 573, 999 } };
    constexpr f64 SLACK = 1e-3;         // world units: the position is rounded to f32
    constexpr f64 OUTWARD_SLACK = 1e-5; // radians
    constexpr f64 SHOWN_SLACK = 1e-6;
    constexpr f64 HALF_WAY = 0.5;
    f64 worstOffRing = 0.0;
    bool still = true;
    f64 worstOutward = 0.0;
    f64 worstShown = 0.0;
    for (const u32 sword : SAMPLED) {
        const auto launch = static_cast<f64>(core::LaunchTick(VOLLEY, sword));
        const auto departure = static_cast<f64>(DepartureTick(VOLLEY, sword));
        const auto out = static_cast<f64>(SWORDS.outTicks);
        const auto hold = static_cast<f64>(SWORDS.holdTicks);
        const core::Vec2 ring = core::SwordAt(VOLLEY, sword, launch, MovingAim(launch)).position;
        worstOffRing = std::max(worstOffRing, std::abs(Distance(ORIGIN, ring) - RingOf(VOLLEY, sword)));
        // Appearing, at its middle and its end, holding, and on the tick it leaves.
        for (const f64 tick :
             { launch, launch + (out * HALF_WAY), launch + out, launch + out + (hold * HALF_WAY), departure }) {
            const core::SwordPose pose = core::SwordAt(VOLLEY, sword, tick, MovingAim(tick));
            still = still && std::bit_cast<u64>(pose.position) == std::bit_cast<u64>(ring);
            worstOutward = std::max(worstOutward, Turn(pose.heading, ring - ORIGIN));
            const f64 shown = std::min(1.0, (tick - launch) / out); // fading in over `outTicks`, from nothing
            worstShown = std::max(worstShown, std::abs(static_cast<f64>(pose.shown) - shown));
        }
    }
    CHECK(worstOffRing < SLACK);
    CHECK(still);
    CHECK(worstOutward < OUTWARD_SLACK);
    CHECK(worstShown < SHOWN_SLACK);
}

TEST_CASE("the rings are regular sunbursts") {
    // Every sword on its wave's radius, the neighbours round each ring evenly spaced, each pointing straight out while it
    // holds.
    constexpr f64 SLACK = 1e-3;         // world units: the position is rounded to f32
    constexpr f64 GAP_SLACK = 1e-6;     // radians
    constexpr f64 OUTWARD_SLACK = 1e-5; // radians
    constexpr f64 HALF = 0.5;
    std::array<std::vector<f64>, SWORDS.waves> angles{};
    f64 worstOffRing = 0.0;
    f64 worstOutward = 0.0;
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        const f64 holding =
            static_cast<f64>(core::LaunchTick(VOLLEY, sword) + SWORDS.outTicks) + (static_cast<f64>(SWORDS.holdTicks) * HALF);
        const core::SwordPose pose = core::SwordAt(VOLLEY, sword, holding, POINT);
        const core::Vec2 out = pose.position - ORIGIN;
        worstOffRing = std::max(worstOffRing, std::abs(Distance({}, out) - RingOf(VOLLEY, sword)));
        worstOutward = std::max(worstOutward, Turn(pose.heading, out));
        angles.at(core::WaveOf(VOLLEY, sword)).push_back(AngleOf(out));
    }
    f64 worstGap = 0.0; // from an even spacing
    for (std::vector<f64>& wave : angles) {
        REQUIRE(wave.size() > 1);
        const f64 spacing = FULL_TURN / static_cast<f64>(wave.size());
        std::ranges::sort(wave);
        worstGap = std::max(worstGap, std::abs(wave.front() + FULL_TURN - wave.back() - spacing));
        for (std::size_t index = 1; index < wave.size(); ++index) {
            worstGap = std::max(worstGap, std::abs(wave[index] - wave[index - 1] - spacing));
        }
    }
    CHECK(worstOffRing < SLACK);
    CHECK(worstGap < GAP_SLACK);
    CHECK(worstOutward < OUTWARD_SLACK);
}

TEST_CASE("a ring draws itself clockwise") {
    // Within each wave, going clockwise round the ring from the wave's first sword, the swords appear one after another:
    // none appears before one it follows.
    std::array<std::vector<std::pair<f64, u64>>, SWORDS.waves> rings{}; // clockwise from the first, and the appearance
    std::array<f64, SWORDS.waves> first{};
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        const u32 wave = core::WaveOf(VOLLEY, sword);
        const f64 angle = AngleOf(RingPoint(VOLLEY, sword) - ORIGIN);
        if (rings.at(wave).empty()) {
            first.at(wave) = angle; // a wave's first index appears first
        }
        rings.at(wave).emplace_back(ClockwiseFrom(first.at(wave), angle), core::LaunchTick(VOLLEY, sword));
    }
    std::size_t outOfTurn = 0;
    for (std::vector<std::pair<f64, u64>>& ring : rings) {
        std::ranges::sort(ring);
        for (std::size_t index = 1; index < ring.size(); ++index) {
            outOfTurn += ring[index].second < ring[index - 1].second ? 1U : 0U;
        }
    }
    CHECK(outOfTurn == 0);
}

TEST_CASE("swords leave their ring at different times") {
    // Each sword waits on its ring a delay of its own after the hold, spread over `departJitterTicks`: every delay that
    // can be drawn is, and neighbours on a ring seldom leave together.
    constexpr f64 MOST_TOGETHER = 0.2; // of neighbouring pairs; at random, 1 in 15
    std::vector<bool> drawn(SWORDS.departJitterTicks, false);
    u64 longest = 0;
    u32 together = 0;
    u32 pairs = 0;
    u64 previous = 0;
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        const u64 delay = DepartureTick(VOLLEY, sword) - core::LaunchTick(VOLLEY, sword) - SWORDS.outTicks - SWORDS.holdTicks;
        longest = std::max(longest, delay);
        if (delay < drawn.size()) {
            drawn[delay] = true;
        }
        if (sword > 0 && core::WaveOf(VOLLEY, sword) == core::WaveOf(VOLLEY, sword - 1)) {
            ++pairs;
            together += delay == previous ? 1U : 0U;
        }
        previous = delay;
    }
    CHECK(longest < SWORDS.departJitterTicks);
    CHECK(std::ranges::all_of(drawn, [](bool some) { return some; }));
    CHECK(static_cast<f64>(together) <= MOST_TOGETHER * static_cast<f64>(pairs));
}

TEST_CASE("swords that don't split evenly over the waves go one more to each of the first waves") {
    constexpr u32 UNEVEN = 1'003; // 3 more than 5 waves of 200
    core::Volley volley = VOLLEY;
    volley.shape.count = UNEVEN;
    std::array<u32, SWORDS.waves> sizes{};
    std::array<u64, SWORDS.waves> earliest{};
    earliest.fill(std::numeric_limits<u64>::max());
    u32 last = 0;
    for (u32 sword = 0; sword < UNEVEN; ++sword) {
        CAPTURE(sword);
        const u32 wave = core::WaveOf(volley, sword);
        REQUIRE(wave < SWORDS.waves);
        // Each wave a run of indices, in order.
        CHECK(wave >= last);
        CHECK(wave <= last + 1);
        last = wave;
        ++sizes.at(wave);
        earliest.at(wave) = std::min(earliest.at(wave), core::LaunchTick(volley, sword));
    }
    CHECK(sizes == std::array<u32, SWORDS.waves>{ { 201, 201, 201, 200, 200 } });
    for (std::size_t wave = 0; wave < SWORDS.waves; ++wave) {
        CAPTURE(wave);
        CHECK(earliest.at(wave) == START + (wave * SWORDS.waveGapTicks));
    }
}

TEST_CASE("each wave forms further out, in a scattered band") {
    constexpr f64 SLACK = 1e-3; // world units: the position is rounded to f32
    // A band of ±scatter evenly filled has a standard deviation of scatter / √3 of the ring's radius; a line has none.
    constexpr f64 LEAST_BAND = 0.4;
    // Across the volley, the distances spread over the rings, not round one circle.
    constexpr f64 LEAST_SPREAD = 1.0; // of a ring step
    std::array<std::vector<f64>, SWORDS.waves> bands{};
    std::vector<f64> all;
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        CAPTURE(sword);
        const f64 ring = RingOf(LOOSE, sword);
        const f64 away = Distance(ORIGIN, RingPoint(LOOSE, sword));
        CHECK(std::abs(away - ring) <= (static_cast<f64>(LOOSE.shape.scatter) * ring) + SLACK);
        bands.at(core::WaveOf(LOOSE, sword)).push_back(away);
        all.push_back(away);
    }
    for (std::size_t wave = 0; wave < SWORDS.waves; ++wave) {
        CAPTURE(wave);
        const f64 ring = static_cast<f64>(SWORDS.ringRadius) + (static_cast<f64>(wave) * static_cast<f64>(SWORDS.ringStep));
        const f64 band = StandardDeviation(bands.at(wave)); // named: MSVC's C4866 (tests/AGENTS.md)
        CHECK(band >= LEAST_BAND * static_cast<f64>(LOOSE.shape.scatter) * ring);
        if (wave > 0) {
            const f64 mean = Mean(bands.at(wave));
            const f64 inner = Mean(bands.at(wave - 1));
            CHECK(mean > inner);
        }
    }
    const f64 spread = StandardDeviation(all);
    CHECK(spread >= LEAST_SPREAD * static_cast<f64>(SWORDS.ringStep));
}

TEST_CASE("places on a ring are jittered, not evenly spaced") {
    // Each wave still goes all the way round: its widest gap is a twelfth of a turn at most.
    constexpr f64 WIDEST_GAP = FULL_TURN / 12.0;
    // The gaps between neighbours differ by at least this much of the even spacing; evenly spaced, they wouldn't at all.
    constexpr f64 LEAST_UNEVEN = 0.5;
    std::array<std::vector<f64>, SWORDS.waves> angles{};
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        const core::Vec2 out = RingPoint(LOOSE, sword) - ORIGIN;
        angles.at(core::WaveOf(LOOSE, sword)).push_back(core::math::Atan2(static_cast<f64>(out.y), static_cast<f64>(out.x)));
    }
    for (std::vector<f64>& wave : angles) {
        CAPTURE(wave.size());
        REQUIRE(wave.size() > 1);
        std::ranges::sort(wave);
        std::vector<f64> gaps{ wave.front() + FULL_TURN - wave.back() };
        for (std::size_t index = 1; index < wave.size(); ++index) {
            gaps.push_back(wave[index] - wave[index - 1]);
        }
        CHECK(std::ranges::max(gaps) < WIDEST_GAP);
        const f64 spacing = FULL_TURN / static_cast<f64>(wave.size());
        const f64 uneven = StandardDeviation(gaps);
        CHECK(uneven >= LEAST_UNEVEN * spacing);
    }
}

TEST_CASE("flights bend by different amounts") {
    // Each first-wave sword's widest bend, as a fraction of its chord, from its ring point to a still aim at the point:
    // the first ring lies well inside the point's distance, so the chords are alike. With the same share of the sweep
    // for every sword, the fractions differ only as the chords' lengths do (by less than SAME_SHARE from least to most);
    // with the shipped jitter, by at least LEAST_RANGE.
    constexpr f64 SAME_SHARE = 1.5;
    constexpr f64 LEAST_RANGE = 2.5;
    const auto range = [](const core::Volley& volley) {
        std::vector<f64> bends;
        for (u32 sword = 0; sword < volley.shape.count; ++sword) {
            if (core::WaveOf(volley, sword) != 0) {
                continue;
            }
            const u64 ring = DepartureTick(volley, sword);
            const core::Vec2 ringPoint = RingPoint(volley, sword);
            f64 farthest = 0.0;
            for (u32 tick = 0; tick <= volley.shape.flightTicks; ++tick) {
                const core::Vec2 position = core::SwordAt(volley, sword, static_cast<f64>(ring + tick), POINT).position;
                farthest = std::max(farthest, OffLine(position, ringPoint, POINT));
            }
            bends.push_back(farthest / Distance(ringPoint, POINT));
        }
        return std::ranges::max(bends) / std::ranges::min(bends);
    };
    core::Volley same = VOLLEY;
    same.shape.sweepJitter = 0.0F;
    CHECK(range(same) < SAME_SHARE);
    CHECK(range(VOLLEY) >= LEAST_RANGE);
}

TEST_CASE("the path is smooth") {
    // A cast at a point inside the ring, its target there. Static, so its lambda needs no capture: Clang wants one of a
    // local, and MSVC's C5258 calls it needless.
    static constexpr core::Vec2 INSIDE{ .x = ORIGIN.x + 90.0F, .y = ORIGIN.y + 40.0F };
    constexpr core::Volley CLOSE{ .startTick = START, .seed = SEED, .origin = ORIGIN, .point = INSIDE, .shape = SWORDS };
    const auto insideAim = [](f64 /*tick*/) { return INSIDE; };
    for (const u32 sword : SAMPLED_SWORDS) {
        CAPTURE(sword);
        const char* aim = "still";
        CAPTURE(aim);
        CheckSmooth(VOLLEY, sword, StillAim);
        aim = "moving";
        CheckSmooth(VOLLEY, sword, MovingAim);
        aim = "inside the ring";
        CheckSmooth(CLOSE, sword, insideAim);
    }
    // The sword whose ring point lies nearest the point: an outer ring's, with the shortest flight and the tightest
    // bend.
    u32 nearest = 0;
    for (u32 sword = 1; sword < SWORDS.count; ++sword) {
        if (Distance(RingPoint(VOLLEY, sword), POINT) < Distance(RingPoint(VOLLEY, nearest), POINT)) {
            nearest = sword;
        }
    }
    CAPTURE(nearest);
    const f64 shortest = Distance(RingPoint(VOLLEY, nearest), POINT);
    CAPTURE(shortest);
    const char* nearestAim = "still";
    CAPTURE(nearestAim);
    CheckSmooth(VOLLEY, nearest, StillAim);
    nearestAim = "moving";
    CheckSmooth(VOLLEY, nearest, MovingAim);
}

TEST_CASE("a sword turns from its ring to its flight without flipping while its target moves off the point") {
    // In each wave, the sword facing most nearly away from the point, which turns furthest (about half a turn) and so
    // flips most easily, and its neighbours either side, and the sword facing the point.
    std::vector<u32> sampled;
    const core::Vec2 towardsPoint = POINT - ORIGIN;
    for (u32 wave = 0; wave < SWORDS.waves; ++wave) {
        u32 away = SWORDS.count;
        u32 towards = SWORDS.count;
        for (u32 sword = 0; sword < SWORDS.count; ++sword) {
            if (core::WaveOf(VOLLEY, sword) != wave) {
                continue;
            }
            const f64 facing = Turn(RingPoint(VOLLEY, sword) - ORIGIN, towardsPoint);
            if (away == SWORDS.count || facing > Turn(RingPoint(VOLLEY, away) - ORIGIN, towardsPoint)) {
                away = sword;
            }
            if (towards == SWORDS.count || facing < Turn(RingPoint(VOLLEY, towards) - ORIGIN, towardsPoint)) {
                towards = sword;
            }
        }
        REQUIRE(away > 0);
        REQUIRE(away + 1 < SWORDS.count);
        sampled.insert(sampled.end(), { away - 1, away, away + 1, towards });
    }
    // From a tick before it leaves its ring through its turn and well into its flight.
    constexpr u64 BEFORE = 1;
    constexpr u64 INTO_FLIGHT = 16;
    const auto timeOf = [](u64 sample) { return static_cast<f64>(sample) * SUBSTEP; };
    f64 mostTurn = 0.0;
    for (const u32 sword : sampled) {
        const u64 departure = DepartureTick(VOLLEY, sword);
        const u64 first = (departure - BEFORE) * SUBSTEPS;
        core::Vec2 last = core::SwordAt(VOLLEY, sword, timeOf(first), MovingAim(timeOf(first))).heading;
        for (u64 sample = first + 1; sample <= (departure + INTO_FLIGHT) * SUBSTEPS; ++sample) {
            const core::Vec2 heading = core::SwordAt(VOLLEY, sword, timeOf(sample), MovingAim(timeOf(sample))).heading;
            mostTurn = std::max(mostTurn, Turn(last, heading));
            last = heading;
        }
    }
    CHECK(mostTurn < MOST_TURN);
}

TEST_CASE("the path curves") {
    // Every sword bends at least 5/16 of the least share of the sweep (1 − sweepJitter), the margin this test held at a
    // quarter of the sweep when the least share was 0.8 and every chord was longer than the sweep. A flight shorter than
    // the sweep (an outer ring's sword forming near the point) bends by a share of its chord instead, so the floor is a
    // share of the shorter of the two.
    constexpr f64 LEAST_SHARE = (1.0 - static_cast<f64>(SWORDS.sweepJitter)) * 5.0 / 16.0;
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        CAPTURE(sword);
        const u64 departure = DepartureTick(VOLLEY, sword);
        const core::Vec2 ringPoint = RingPoint(VOLLEY, sword);
        f64 farthest = 0.0;
        for (u32 tick = 0; tick <= SWORDS.flightTicks; ++tick) {
            const core::Vec2 position = core::SwordAt(VOLLEY, sword, static_cast<f64>(departure + tick), POINT).position;
            farthest = std::max(farthest, OffLine(position, ringPoint, POINT));
        }
        CHECK(farthest >= LEAST_SHARE * std::min(Distance(ringPoint, POINT), static_cast<f64>(SWORDS.sweep)));
    }
}

TEST_CASE("each sword aims at its own spot a little off its target's middle, the same at every moment") {
    constexpr core::Vec2 BEFORE{ .x = 600.0F, .y = 0.0F };
    constexpr core::Vec2 AFTER{ .x = 640.0F, .y = 20.0F };
    constexpr core::Vec2 DEAD_AT{ .x = 520.0F, .y = 40.0F };
    const std::array<core::VolleyTarget, 1> living{
        { { .entity = core::StableId{ 1 }, .previous = BEFORE, .current = AFTER } }
    };
    const std::array<core::VolleyTarget, 1> dead{
        { { .entity = core::StableId{ 1 }, .previous = DEAD_AT, .current = DEAD_AT, .diedTick = START } }
    };
    std::vector<std::pair<f32, f32>> spots;
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        CAPTURE(sword);
        const core::Vec2 offset = core::AimOffset(VOLLEY, sword);
        CHECK(Distance({}, offset) <= static_cast<f64>(core::AIM_SCATTER));
        const core::Vec2 aim = core::SwordAim(VOLLEY, sword, START + 1, 1.0F, living);
        CHECK(aim.x == AFTER.x + offset.x);
        CHECK(aim.y == AFTER.y + offset.y);
        // Where it plunges, too: never stepped on at a death.
        const core::Vec2 plunge = core::SwordAim(VOLLEY, sword, START + SWORDS.flightTicks, 1.0F, dead);
        CHECK(plunge.x == DEAD_AT.x + offset.x);
        CHECK(plunge.y == DEAD_AT.y + offset.y);
        spots.emplace_back(offset.x, offset.y);
    }
    // Spread evenly over the disc: spots evenly spread over a disc lie 2/3 of its radius from its middle on average, and
    // few lie on its rim (1 − 0.99² ≈ 2% beyond 99% of the radius).
    constexpr f64 EVEN_MEAN = 2.0 / 3.0;
    constexpr f64 MEAN_SLACK = 0.1;
    constexpr f64 RIM = 0.99;
    constexpr f64 MOST_ON_RIM = 0.05;
    const auto scatter = static_cast<f64>(core::AIM_SCATTER);
    f64 sum = 0.0;
    std::size_t onRim = 0;
    for (const auto& [across, down] : spots) {
        const f64 reach = Distance({}, { .x = across, .y = down });
        sum += reach;
        onRim += reach >= RIM * scatter ? 1U : 0U;
    }
    const f64 mean = sum / static_cast<f64>(spots.size());
    CHECK(std::abs(mean - (EVEN_MEAN * scatter)) <= MEAN_SLACK * EVEN_MEAN * scatter);
    CHECK(static_cast<f64>(onRim) <= MOST_ON_RIM * static_cast<f64>(spots.size()));
    // Not piled on one point.
    std::ranges::sort(spots);
    const auto [last, end] = std::ranges::unique(spots);
    constexpr std::ptrdiff_t MOST_SHARED = 10;
    CHECK(end - last < MOST_SHARED);
}

TEST_CASE("the end follows a moving aim") {
    constexpr core::Vec2 NEAR{ .x = 420.0F, .y = 260.0F };
    for (const u32 sword : SAMPLED_SWORDS) {
        CAPTURE(sword);
        const f64 arrival = static_cast<f64>(core::ArrivalTick(VOLLEY, sword));
        const core::SwordPose atPoint = core::SwordAt(VOLLEY, sword, arrival, POINT);
        const core::SwordPose atNear = core::SwordAt(VOLLEY, sword, arrival, NEAR);
        CHECK(atPoint.position.x == POINT.x);
        CHECK(atPoint.position.y == POINT.y);
        CHECK(atNear.position.x == NEAR.x);
        CHECK(atNear.position.y == NEAR.y);
        // An eighth of a tick before, still in flight, it is all but there, wherever the aim is.
        constexpr f64 CLOSE = 2.0;
        CHECK(Distance(core::SwordAt(VOLLEY, sword, arrival - SUBSTEP, POINT).position, POINT) < CLOSE);
        CHECK(Distance(core::SwordAt(VOLLEY, sword, arrival - SUBSTEP, NEAR).position, NEAR) < CLOSE);
    }
}

TEST_CASE("a sword fades in at its place from its launch and fades after its arrival, staying at the aim") {
    constexpr u32 SWORD = 499;
    constexpr f64 SLACK = 1e-6;
    constexpr f64 HALF_SHOWN = 0.5;
    const f64 launch = static_cast<f64>(core::LaunchTick(VOLLEY, SWORD));
    const f64 arrival = static_cast<f64>(core::ArrivalTick(VOLLEY, SWORD));
    const f64 fade = static_cast<f64>(SWORDS.fadeTicks);
    // Unseen at its place on its ring before its launch, then all there once it has appeared.
    const core::SwordPose waiting = core::SwordAt(VOLLEY, SWORD, launch - SUBSTEP, POINT);
    const core::Vec2 ring = RingPoint(VOLLEY, SWORD);
    CHECK(waiting.shown == 0.0F);
    CHECK(waiting.position.x == ring.x);
    CHECK(waiting.position.y == ring.y);
    CHECK(core::SwordAt(VOLLEY, SWORD, launch, POINT).shown == 0.0F);
    CHECK(core::SwordAt(VOLLEY, SWORD, launch + static_cast<f64>(SWORDS.outTicks), POINT).shown == 1.0F);
    CHECK(core::SwordAt(VOLLEY, SWORD, arrival, POINT).shown == 1.0F);
    const core::SwordPose fading = core::SwordAt(VOLLEY, SWORD, arrival + (fade * HALF_SHOWN), POINT);
    CHECK(std::abs(static_cast<f64>(fading.shown) - HALF_SHOWN) < SLACK);
    CHECK(fading.position.x == POINT.x);
    CHECK(fading.position.y == POINT.y);
    CHECK(core::SwordAt(VOLLEY, SWORD, arrival + fade, POINT).shown == 0.0F);
    CHECK(core::SwordAt(VOLLEY, SWORD, arrival + fade + 1.0, POINT).shown == 0.0F);
}

TEST_CASE("a volley of one with no ring or sweep flies straight to its aim, eased") {
    constexpr core::VolleyShape BOLT{ .count = 1, .ringRadius = 0.0F, .flightTicks = 20, .sweep = 0.0F };
    constexpr core::Volley VOLLEY_OF_ONE{ .startTick = START, .seed = SEED, .origin = ORIGIN, .point = POINT, .shape = BOLT };
    constexpr core::Vec2 TARGET{ .x = 660.0F, .y = -20.0F }; // the enemy nearest the point
    constexpr f64 SLACK = 1e-3;
    const f64 length = Distance(ORIGIN, TARGET);
    f64 last = -1.0;
    for (u64 sample = 0; sample <= BOLT.flightTicks * SUBSTEPS; ++sample) {
        const f64 tick = static_cast<f64>(START) + (static_cast<f64>(sample) * SUBSTEP);
        const core::SwordPose pose = core::SwordAt(VOLLEY_OF_ONE, 0, tick, TARGET);
        CHECK(OffLine(pose.position, ORIGIN, TARGET) < SLACK);
        CHECK(Turn(pose.heading, TARGET - ORIGIN) < SLACK);
        const f64 along = Distance(ORIGIN, pose.position);
        CHECK(along >= last);
        last = along;
    }
    const f64 firstStep = Distance(ORIGIN, core::SwordAt(VOLLEY_OF_ONE, 0, static_cast<f64>(START) + 1.0, TARGET).position);
    CHECK(firstStep < length / static_cast<f64>(BOLT.flightTicks)); // it starts slowly
    CHECK(last == doctest::Approx(length));
}

TEST_CASE("a degenerate volley stays finite") {
    constexpr core::VolleyShape INSTANT{ .count = 1 };
    constexpr core::Volley AT_ONCE{ .startTick = START, .seed = SEED, .origin = ORIGIN, .point = ORIGIN, .shape = INSTANT };
    constexpr f64 BETWEEN = 0.5;
    for (const f64 tick : { 0.0, static_cast<f64>(START), static_cast<f64>(START) + BETWEEN, static_cast<f64>(START + 1) }) {
        for (const core::Vec2 aim : { ORIGIN, POINT }) {
            const core::SwordPose pose = core::SwordAt(AT_ONCE, 0, tick, aim);
            CHECK(core::IsFinite(pose.position));
            CHECK(IsUnit(pose.heading));
            CHECK(std::isfinite(pose.shown));
        }
    }
    CHECK(core::ArrivalTick(AT_ONCE, 0) == START);
    const core::SwordPose arrived = core::SwordAt(AT_ONCE, 0, static_cast<f64>(START), POINT);
    CHECK(arrived.position.x == POINT.x);
    CHECK(arrived.position.y == POINT.y);
    constexpr core::Volley EMPTY{ .startTick = START, .shape = {} };
    CHECK(core::LastArrival(EMPTY) == START);
}

TEST_CASE("the same volley gives the same positions bit for bit") {
    constexpr std::array<f64, 3> TICKS{
        { static_cast<f64>(START) + 20.375, static_cast<f64>(START) + 50.0, static_cast<f64>(START) + 71.875 }
    };
    const core::Volley copy = VOLLEY;
    u64 latest = 0;
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        latest = std::max(latest, core::ArrivalTick(VOLLEY, sword));
    }
    CHECK(core::LastArrival(VOLLEY) == latest);
    // Backwards through the swords the second time, so nothing remembered from one call can help the next.
    for (const f64 tick : TICKS) {
        for (u32 sword = 0; sword < SWORDS.count; ++sword) {
            const u32 other = SWORDS.count - 1 - sword;
            const core::SwordPose first = core::SwordAt(VOLLEY, other, tick, MovingAim(tick));
            const core::SwordPose second = core::SwordAt(copy, other, tick, MovingAim(tick));
            CHECK(std::bit_cast<u64>(first.position) == std::bit_cast<u64>(second.position));
            CHECK(std::bit_cast<u64>(first.heading) == std::bit_cast<u64>(second.heading));
            CHECK(std::bit_cast<u32>(first.shown) == std::bit_cast<u32>(second.shown));
        }
    }
}

TEST_CASE("hits split evenly over the tagged targets") {
    const std::array<core::VolleyTarget, 4> targets = FourTargets();
    const std::array<u32, 4> counts = CountTargets(START + SWORDS.flightTicks, targets);
    CHECK(counts == std::array<u32, 4>{ { 250, 250, 250, 250 } });
}

TEST_CASE("a dead target's swords move to the living, the others stay") {
    std::array<core::VolleyTarget, 4> targets = FourTargets();
    targets[1].diedTick = FIRST_DEATH;
    const std::span<const core::VolleyTarget> view = targets;
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        CHECK(core::SwordTarget(VOLLEY, sword, FIRST_DEATH - 1, view) == sword % 4);
        if (sword % 4 != 1) {
            CHECK(core::SwordTarget(VOLLEY, sword, FIRST_DEATH, view) == sword % 4);
        }
    }
    // Target 1's 250 swords spread over the living 0, 2 and 3 as 84, 83 and 83, in some order.
    const std::array<u32, 4> counts = CountTargets(FIRST_DEATH, targets);
    CHECK(counts[1] == 0);
    std::array<u32, 3> living{ { counts[0], counts[2], counts[3] } };
    std::ranges::sort(living);
    CHECK(living == std::array<u32, 3>{ { 333, 333, 334 } });
}

TEST_CASE("a sword moved by one death keeps its new target through the next while that lives") {
    std::array<core::VolleyTarget, 4> targets = FourTargets();
    targets[1].diedTick = FIRST_DEATH;
    targets[2].diedTick = SECOND_DEATH;
    const std::span<const core::VolleyTarget> view = targets;
    std::array<u32, 4> movedTo{};
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        CAPTURE(sword);
        const u32 first = core::SwordTarget(VOLLEY, sword, FIRST_DEATH, view);
        const u32 second = core::SwordTarget(VOLLEY, sword, SECOND_DEATH, view);
        if (sword % 4 == 1) {
            ++movedTo.at(first);
        }
        if (first != 2) {
            CHECK(second == first);
        }
        else if (SECOND_DEATH + SWORDS.commitTicks <= core::ArrivalTick(VOLLEY, sword)) {
            CHECK((second == 0 || second == 3));
        }
        else {
            CHECK(second == 2); // too close to its arrival to move on: it plunges at 2's frozen point
        }
    }
    // Some of target 1's swords went to 3 and stayed there.
    CHECK(movedTo[3] > 0);
}

TEST_CASE("moved swords spread over many living targets, not onto the first") {
    constexpr std::size_t TARGETS = 500;
    std::vector<core::VolleyTarget> targets(TARGETS, { .previous = POINT, .current = POINT });
    for (std::size_t index = 0; index < TARGETS; index += 2) {
        targets[index].diedTick = FIRST_DEATH;
    }
    std::vector<u32> landed(TARGETS, 0);
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        const u32 target = core::SwordTarget(VOLLEY, sword, FIRST_DEATH, targets);
        REQUIRE(targets[target].diedTick == 0);
        if (sword % TARGETS % 2 == 0) {
            ++landed[target];
        }
    }
    // 500 moved swords over 250 living targets: 2 each on average.
    constexpr u32 MOST_ON_ONE = 12;
    constexpr std::ptrdiff_t LEAST_REACHED = 100;
    CHECK(std::ranges::max(landed) <= MOST_ON_ONE);
    CHECK(std::ranges::count_if(landed, [](u32 swords) { return swords > 0; }) >= LEAST_REACHED);
}

TEST_CASE("with every target dead, each sword plunges at the frozen point of the last it flew at") {
    std::array<core::VolleyTarget, 4> targets = FourTargets();
    constexpr std::array<u64, 4> DEATHS{ { 640, 620, 650, 630 } };
    for (std::size_t index = 0; index < targets.size(); ++index) {
        targets.at(index).diedTick = DEATHS.at(index);
    }
    const std::span<const core::VolleyTarget> view = targets;
    std::array<u32, 4> lasts{};
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        CAPTURE(sword);
        const u64 arrival = core::ArrivalTick(VOLLEY, sword);
        const u32 last = core::SwordTarget(VOLLEY, sword, arrival, view);
        ++lasts.at(last);
        // It never goes back to a target that died before the one it ended on.
        CHECK(core::SwordTarget(VOLLEY, sword, DEATHS[2] - 1, view) == last);
        for (const u64 tick : { arrival, arrival + SWORDS.fadeTicks }) {
            const core::Vec2 aim = core::SwordAim(VOLLEY, sword, tick, 1.0F, view);
            const core::Vec2 position = core::SwordAt(VOLLEY, sword, static_cast<f64>(tick), aim).position;
            const core::Vec2 spot = Spot(targets.at(last).current, sword);
            CHECK(position.x == spot.x);
            CHECK(position.y == spot.y);
        }
    }
    // The last to die, 2, ends every chain.
    CHECK(lasts == std::array<u32, 4>{ { 0, 0, SWORDS.count, 0 } });
}

TEST_CASE("a sword's aim and position never jump when its target dies") {
    std::array<core::VolleyTarget, 4> targets = FourTargets();
    targets[1].diedTick = FIRST_DEATH;
    targets[2].diedTick = SECOND_DEATH;
    const std::span<const core::VolleyTarget> view = targets;
    // Swords of target 1, which move once, or twice when they first moved to 2: two of the first wave's, one of each later
    // wave's (a run of count / waves swords each, a multiple of the four targets), and the first that moves twice, for
    // the chained swing. A first-wave sword arrives too soon after the second death to move again.
    constexpr u32 PER_WAVE = SWORDS.count / SWORDS.waves;
    static_assert(PER_WAVE % 4 == 0);
    constexpr u32 NEXT_OF_TARGET_ONE = 1 + 4;
    std::vector<u32> sampled{ 1, NEXT_OF_TARGET_ONE };
    for (u32 wave = 1; wave < SWORDS.waves; ++wave) {
        sampled.push_back(1 + (wave * PER_WAVE));
    }
    u32 twice = 1;
    while (twice < SWORDS.count && (core::SwordTarget(VOLLEY, twice, FIRST_DEATH, view) != 2 ||
                                    core::SwordTarget(VOLLEY, twice, SECOND_DEATH, view) == 2)) {
        twice += 4;
    }
    REQUIRE(twice < SWORDS.count);
    sampled.push_back(twice);
    for (const u32 sword : sampled) {
        REQUIRE(sword % 4 == 1);
        CAPTURE(sword);
        CheckSmooth(VOLLEY, sword, [view, sword](f64 tick) { return AimAt(sword, tick, view); });
    }
}

TEST_CASE("SwordTarget and SwordAim agree on the target at every moment") {
    constexpr u64 LATER_DEATH = 700; // a whole swing after the first, so each swing starts from a frozen point
    std::array<core::VolleyTarget, 4> targets = FourTargets();
    targets[1].diedTick = FIRST_DEATH;
    targets[2].diedTick = LATER_DEATH;
    const std::span<const core::VolleyTarget> view = targets;
    constexpr u64 SAMPLES = 120 * SUBSTEPS;
    constexpr f64 SLACK = 1e-3;
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        CAPTURE(sword);
        const u64 arrival = core::ArrivalTick(VOLLEY, sword);
        u64 moved = 0;     // the tick it last moved on from a dead target
        u32 movedFrom = 0; // and that target
        for (u64 sample = 0; sample <= SAMPLES; ++sample) {
            const u64 tick = START + (sample / SUBSTEPS);
            const f64 time = static_cast<f64>(START) + (static_cast<f64>(sample) * SUBSTEP);
            const u32 target = core::SwordTarget(VOLLEY, sword, tick, view);
            const u32 before = core::SwordTarget(VOLLEY, sword, tick - 1, view);
            if (target != before) {
                moved = tick;
                movedFrom = before;
            }
            const core::Vec2 aim = AimAt(sword, time, view);
            const core::Vec2 towards = Spot(targets.at(target).current, sword);
            if (moved == 0) {
                CHECK(aim.x == towards.x);
                CHECK(aim.y == towards.y);
                continue;
            }
            // Swinging from the dead target's frozen point to the new one, eased, and over by the arrival.
            const core::Vec2 from = Spot(targets.at(movedFrom).current, sword);
            const f64 length = std::min(
                std::max(core::RETARGET_TICKS, Distance(from, towards) / core::RETARGET_SPEED),
                static_cast<f64>(arrival - moved)
            );
            const f64 progress = (time - static_cast<f64>(moved)) / length;
            if (progress >= 1.0) {
                CHECK(aim.x == towards.x);
                CHECK(aim.y == towards.y);
            }
            else {
                const f64 eased = progress * progress * (3.0 - (2.0 * progress));
                const core::Vec2 expected = from + ((towards - from) * static_cast<f32>(eased));
                CHECK(Distance(aim, expected) < SLACK);
            }
        }
    }
}

TEST_CASE("a target dying near a sword's arrival leaves the sword where it strikes") {
    constexpr u64 DURING_FADE = 5; // ticks after the arrival
    constexpr u64 PAST_FADE = 10;  // ticks after the fade that are checked too
    for (const u32 sword : SAMPLED_SWORDS) {
        const u64 arrival = core::ArrivalTick(VOLLEY, sword);
        const u32 own = sword % 4;
        for (const u64 died : { arrival - SWORDS.commitTicks - 1,
                                arrival - SWORDS.commitTicks,
                                arrival - SWORDS.commitTicks + 1,
                                arrival,
                                arrival + DURING_FADE }) {
            CAPTURE(sword);
            CAPTURE(died);
            std::array<core::VolleyTarget, 4> targets = FourTargets();
            targets.at(own).diedTick = died;
            const std::span<const core::VolleyTarget> view = targets;
            // It moves on only when its target died at least the volley's commit window before its arrival.
            const u32 struck = core::SwordTarget(VOLLEY, sword, arrival, view);
            CHECK((struck != own) == (died + SWORDS.commitTicks <= arrival));
            // At its arrival it is exactly on the target it strikes, or that one's frozen point.
            const core::Vec2 aim = core::SwordAim(VOLLEY, sword, arrival, 1.0F, view);
            const core::Vec2 position = core::SwordAt(VOLLEY, sword, static_cast<f64>(arrival), aim).position;
            const core::Vec2 spot = Spot(targets.at(struck).current, sword);
            CHECK(position.x == spot.x);
            CHECK(position.y == spot.y);
            // Through its fade it stays there, on the same target, whatever dies.
            for (u64 sample = 1; sample <= (SWORDS.fadeTicks + PAST_FADE) * SUBSTEPS; ++sample) {
                const f64 time = static_cast<f64>(arrival) + (static_cast<f64>(sample) * SUBSTEP);
                const core::Vec2 fading = core::SwordAt(VOLLEY, sword, time, AimAt(sword, time, view)).position;
                CHECK(fading.x == position.x);
                CHECK(fading.y == position.y);
                CHECK(core::SwordTarget(VOLLEY, sword, arrival + (sample / SUBSTEPS), view) == struck);
            }
        }
    }
}

TEST_CASE("a swing across the whole tagged area at the last moment ends on the new target at the arrival, smoothly") {
    // Two targets 500 units apart, nearly the tag's diameter, the first dying exactly the commit window before the
    // arrival: the swing at RETARGET_SPEED fits in the window, so it moves the sword at the designed pace at most.
    constexpr f32 HALF_APART = 250.0F;
    constexpr core::Vec2 LEFT{ .x = POINT.x - HALF_APART, .y = POINT.y };
    constexpr core::Vec2 RIGHT{ .x = POINT.x + HALF_APART, .y = POINT.y };
    for (const u32 sword : { 0U, 498U }) { // both fly at target 0 first
        CAPTURE(sword);
        const u64 arrival = core::ArrivalTick(VOLLEY, sword);
        const std::array<core::VolleyTarget, 2> targets{ {
            { .entity = core::StableId{ 1 }, .previous = LEFT, .current = LEFT, .diedTick = arrival - SWORDS.commitTicks },
            { .entity = core::StableId{ 2 }, .previous = RIGHT, .current = RIGHT },
        } };
        const std::span<const core::VolleyTarget> view = targets;
        CHECK(core::SwordTarget(VOLLEY, sword, arrival, view) == 1);
        const core::Vec2 aim = core::SwordAim(VOLLEY, sword, arrival, 1.0F, view);
        const core::Vec2 position = core::SwordAt(VOLLEY, sword, static_cast<f64>(arrival), aim).position;
        CHECK(position.x == Spot(RIGHT, sword).x);
        CHECK(position.y == Spot(RIGHT, sword).y);
        const f64 swingTicks = Distance(LEFT, RIGHT) / core::RETARGET_SPEED;
        CHECK(swingTicks <= static_cast<f64>(SWORDS.commitTicks));
        const auto died = static_cast<f64>(targets[0].diedTick);
        const SwingBound swing{ .from = died, .until = died + swingTicks, .peak = core::RETARGET_PEAK * SUBSTEP };
        CheckSmooth(VOLLEY, sword, [view, sword](f64 tick) { return AimAt(sword, tick, view); }, swing);
    }
}

TEST_CASE("a swing too long for the time left still ends on the new target at the arrival") {
    // The targets have walked apart, far beyond the tag's area: the way at RETARGET_SPEED takes longer than the commit
    // window left, so the swing is cut short to end at the arrival, and moves the sword at that shorter swing's pace.
    constexpr f32 HALF_APART = 400.0F;
    constexpr core::Vec2 LEFT{ .x = POINT.x - HALF_APART, .y = POINT.y };
    constexpr core::Vec2 RIGHT{ .x = POINT.x + HALF_APART, .y = POINT.y };
    for (const u32 sword : { 0U, 498U }) { // both fly at target 0 first
        CAPTURE(sword);
        const u64 arrival = core::ArrivalTick(VOLLEY, sword);
        const std::array<core::VolleyTarget, 2> targets{ {
            { .entity = core::StableId{ 1 }, .previous = LEFT, .current = LEFT, .diedTick = arrival - SWORDS.commitTicks },
            { .entity = core::StableId{ 2 }, .previous = RIGHT, .current = RIGHT },
        } };
        const std::span<const core::VolleyTarget> view = targets;
        REQUIRE(Distance(LEFT, RIGHT) / core::RETARGET_SPEED > static_cast<f64>(SWORDS.commitTicks));
        CHECK(core::SwordTarget(VOLLEY, sword, arrival, view) == 1);
        const core::Vec2 aim = core::SwordAim(VOLLEY, sword, arrival, 1.0F, view);
        const core::Vec2 position = core::SwordAt(VOLLEY, sword, static_cast<f64>(arrival), aim).position;
        CHECK(position.x == Spot(RIGHT, sword).x);
        CHECK(position.y == Spot(RIGHT, sword).y);
        // The same ease over the commit window instead of the way at RETARGET_SPEED.
        const f64 swingSpeed = Distance(LEFT, RIGHT) / static_cast<f64>(SWORDS.commitTicks);
        const SwingBound swing{ .from = static_cast<f64>(targets[0].diedTick),
                                .until = static_cast<f64>(arrival),
                                .peak = swingSpeed * (core::RETARGET_PEAK / core::RETARGET_SPEED) * SUBSTEP };
        CheckSmooth(VOLLEY, sword, [view, sword](f64 tick) { return AimAt(sword, tick, view); }, swing);
    }
}

TEST_CASE("a volley's commit window fits a swing across its tagged area") {
    // 2 × 260 / 16 = 32.5 ticks, rounded up.
    CHECK(core::CommitTicksFor(SWORDS_RADIUS) == 33);
    // Never shorter than two of the shortest swings.
    constexpr f32 SMALL_RADIUS = 40.0F;
    CHECK(core::CommitTicksFor(SMALL_RADIUS) == core::MIN_COMMIT_TICKS);
    CHECK(core::CommitTicksFor(0.0F) == core::MIN_COMMIT_TICKS);
    CHECK(core::MIN_COMMIT_TICKS == 16);
}

TEST_CASE("a living target's aim is interpolated by the frame's alpha") {
    constexpr core::Vec2 BEFORE{ .x = 600.0F, .y = 0.0F };
    constexpr core::Vec2 AFTER{ .x = 640.0F, .y = 20.0F };
    constexpr f32 ALPHA = 0.25F;
    const std::array<core::VolleyTarget, 1> targets{
        { { .entity = core::StableId{ 1 }, .previous = BEFORE, .current = AFTER } }
    };
    constexpr core::Vec2 BETWEEN{ .x = 610.0F, .y = 5.0F }; // a quarter of the way
    const core::Vec2 aim = core::SwordAim(VOLLEY, 0, START + 1, ALPHA, targets);
    CHECK(aim.x == doctest::Approx(Spot(BETWEEN, 0).x));
    CHECK(aim.y == doctest::Approx(Spot(BETWEEN, 0).y));
}

TEST_CASE("with no target tagged, every sword plunges at the point") {
    const std::array<core::VolleyTarget, 1> targets{ { { .previous = POINT, .current = POINT, .diedTick = START } } };
    for (const u32 sword : SAMPLED_SWORDS) {
        const u32 target = core::SwordTarget(VOLLEY, sword, START + SWORDS.flightTicks, targets);
        CHECK(target == 0);
        const core::Vec2 aim = core::SwordAim(VOLLEY, sword, START + SWORDS.flightTicks, 1.0F, targets);
        CHECK(aim.x == Spot(POINT, sword).x);
        CHECK(aim.y == Spot(POINT, sword).y);
    }
}

TEST_CASE("a sword strikes a creature alive as its arrival's tick began, and plunges into the dead and the point") {
    constexpr u64 ARRIVAL = 700;
    constexpr core::StableId FOX{ 9 };
    constexpr core::VolleyTarget LIVING{ .entity = FOX };
    constexpr core::VolleyTarget DIED_THAT_TICK{ .entity = FOX, .diedTick = ARRIVAL }; // after the tick's arrivals
    constexpr core::VolleyTarget DIED_LATER{ .entity = FOX, .diedTick = ARRIVAL + 1 };
    constexpr core::VolleyTarget DIED_BEFORE{ .entity = FOX, .diedTick = ARRIVAL - 1 };
    constexpr core::VolleyTarget THE_POINT{ .diedTick = START };
    CHECK(LIVING.Struck(ARRIVAL));
    CHECK(DIED_THAT_TICK.Struck(ARRIVAL));
    CHECK(DIED_LATER.Struck(ARRIVAL));
    CHECK_FALSE(DIED_BEFORE.Struck(ARRIVAL));
    CHECK_FALSE(THE_POINT.Struck(ARRIVAL));
}

TEST_CASE("a volley is valid with a valid shape and finite points") {
    CHECK(VOLLEY.Valid());
    core::Volley broken = VOLLEY;
    broken.point.x = std::numeric_limits<f32>::infinity();
    CHECK_FALSE(broken.Valid());
    broken = VOLLEY;
    broken.origin.y = std::numeric_limits<f32>::quiet_NaN();
    CHECK_FALSE(broken.Valid());
    broken = VOLLEY;
    broken.shape.count = core::MAX_VOLLEY_COUNT + 1;
    CHECK_FALSE(broken.Valid());
    // A shape read from the data has no commit window yet: valid as data, but not as a cast volley's.
    broken = VOLLEY;
    broken.shape.commitTicks = 0;
    CHECK(broken.shape.Valid());
    CHECK_FALSE(broken.Valid());
}

constexpr f32 NOT_A_NUMBER = std::numeric_limits<f32>::quiet_NaN();
constexpr f32 INFINITE = std::numeric_limits<f32>::infinity();
constexpr f32 NEGATIVE = -1.0F;
constexpr f32 WHOLE = 1.0F;
constexpr f32 ALMOST_WHOLE = 0.999F;

// Whether SWORDS is valid with `change` made to it.
template<typename Change>
bool ValidWith(const Change& change) {
    core::VolleyShape shape = SWORDS;
    change(shape);
    return shape.Valid();
}

TEST_CASE("a volley shape needs a wave, rings and jitters that are numbers, and a scatter and sweep jitter below 1") {
    CHECK(SWORDS.Valid());
    CHECK_FALSE(ValidWith([](core::VolleyShape& shape) { shape.waves = 0; }));
    CHECK_FALSE(ValidWith([](core::VolleyShape& shape) { shape.waves = core::MAX_VOLLEY_COUNT + 1; }));
    CHECK(ValidWith([](core::VolleyShape& shape) { shape.waves = core::MAX_VOLLEY_COUNT; }));
    CHECK_FALSE(ValidWith([](core::VolleyShape& shape) { shape.ringStep = NEGATIVE; }));
    CHECK_FALSE(ValidWith([](core::VolleyShape& shape) { shape.ringStep = INFINITE; }));
    CHECK_FALSE(ValidWith([](core::VolleyShape& shape) { shape.scatter = NOT_A_NUMBER; }));
    CHECK_FALSE(ValidWith([](core::VolleyShape& shape) { shape.scatter = NEGATIVE; }));
    CHECK_FALSE(ValidWith([](core::VolleyShape& shape) { shape.scatter = WHOLE; }));
    CHECK(ValidWith([](core::VolleyShape& shape) { shape.scatter = ALMOST_WHOLE; }));
    CHECK_FALSE(ValidWith([](core::VolleyShape& shape) { shape.angleJitter = NEGATIVE; }));
    CHECK_FALSE(ValidWith([](core::VolleyShape& shape) { shape.angleJitter = INFINITE; }));
    CHECK_FALSE(ValidWith([](core::VolleyShape& shape) { shape.sweepJitter = NOT_A_NUMBER; }));
    CHECK_FALSE(ValidWith([](core::VolleyShape& shape) { shape.sweepJitter = WHOLE; }));
    CHECK(ValidWith([](core::VolleyShape& shape) { shape.sweepJitter = ALMOST_WHOLE; }));
}

TEST_CASE("a cast volley's commit window is valid from the floor up") {
    core::Volley volley = VOLLEY;
    volley.shape.commitTicks = core::MIN_COMMIT_TICKS - 1;
    CHECK_FALSE(volley.Valid());
    volley.shape.commitTicks = core::MIN_COMMIT_TICKS;
    CHECK(volley.Valid());
}

} // namespace
