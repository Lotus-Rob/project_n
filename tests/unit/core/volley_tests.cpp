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
    .ringRadius = 140.0F,
    .staggerTicks = 30,
    .outTicks = 12,
    .flightTicks = 66,
    .sweep = 420.0F,
    .fadeTicks = 18,
    .commitTicks = core::CommitTicksFor(SWORDS_RADIUS),
};
constexpr core::Volley VOLLEY{ .use = 7, .startTick = START, .seed = SEED, .origin = ORIGIN, .point = POINT, .shape = SWORDS };
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

// No step longer than this many times the median step of its phase: the burst out, the flight or the fade.
constexpr f64 MOST_STEP = 3.0;
// No heading turns more than this between samples, in radians.
constexpr f64 MOST_TURN = 0.25;

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

// Checks one sword's path from its launch through its fade, sampled between ticks, for jumps in position and heading,
// flying at `aimAt(tick)`, through the retarget swing `swing` if any.
template<typename AimAt>
void CheckSmooth(const core::Volley& volley, u32 sword, const AimAt& aimAt, SwingBound swing = {}) {
    const u64 launch = core::LaunchTick(volley, sword);
    const u64 ring = launch + volley.shape.outTicks;
    const u64 arrival = core::ArrivalTick(volley, sword);
    const u64 samples = (arrival + volley.shape.fadeTicks - launch) * SUBSTEPS;
    std::array<std::vector<Step>, 3> steps{}; // out, flight, fade
    core::SwordPose last = core::SwordAt(volley, sword, static_cast<f64>(launch), aimAt(static_cast<f64>(launch)));
    f64 mostTurn = 0.0;
    for (u64 sample = 1; sample <= samples; ++sample) {
        const f64 tick = static_cast<f64>(launch) + (static_cast<f64>(sample) * SUBSTEP);
        const core::SwordPose pose = core::SwordAt(volley, sword, tick, aimAt(tick));
        REQUIRE(core::IsFinite(pose.position));
        REQUIRE(IsUnit(pose.heading));
        mostTurn = std::max(mostTurn, Turn(last.heading, pose.heading));
        std::size_t phase = 2;
        if (tick <= static_cast<f64>(ring)) {
            phase = 0;
        }
        else if (tick <= static_cast<f64>(arrival)) {
            phase = 1;
        }
        const bool swinging = tick > swing.from && tick - SUBSTEP < swing.until;
        steps.at(phase).push_back({ .length = Distance(last.position, pose.position), .swinging = swinging });
        last = pose;
    }
    CHECK(mostTurn < MOST_TURN);
    REQUIRE_FALSE(steps[1].empty());
    for (const std::vector<Step>& phase : steps) {
        CHECK(NoJump(phase, swing.peak));
    }
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

// The aim at the render time `time`: the snapshot it leads to and the alpha that reaches it, as the renderer passes them.
core::Vec2 AimAt(u32 sword, f64 time, std::span<const core::VolleyTarget> targets) {
    const f64 snapshot = std::ceil(time);
    return core::SwordAim(VOLLEY, sword, static_cast<u64>(snapshot), static_cast<f32>(time - snapshot + 1.0), targets);
}

// Mid-flight for every sword, and the commit window before the earliest arrival (678), so every sword can move on.
constexpr u64 FIRST_DEATH = 645;
// Mid-flight, near the end of some swings from the first death, and within the commit window of some arrivals.
constexpr u64 SECOND_DEATH = 660;

TEST_CASE("every sword launches within the stagger and arrives after it") {
    std::array<bool, SWORDS.staggerTicks> launched{};
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        const u64 launch = core::LaunchTick(VOLLEY, sword);
        REQUIRE(launch >= START);
        REQUIRE(launch < START + SWORDS.staggerTicks);
        launched.at(launch - START) = true;
        CHECK(core::ArrivalTick(VOLLEY, sword) == launch + SWORDS.outTicks + SWORDS.flightTicks);
    }
    // They stream out over the whole stagger, not all at once.
    CHECK(std::ranges::all_of(launched, [](bool some) { return some; }));
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
}

TEST_CASE("a sword turns on the ring without flipping while its target moves off the point") {
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        CAPTURE(sword);
        // The second half of the burst out, where it turns, and a tick into the flight.
        const u64 turnStart = (core::LaunchTick(VOLLEY, sword) * SUBSTEPS) + ((SWORDS.outTicks * SUBSTEPS) / 2);
        const u64 turnEnd = (core::LaunchTick(VOLLEY, sword) + SWORDS.outTicks + 1) * SUBSTEPS;
        const auto timeOf = [](u64 sample) { return static_cast<f64>(sample) * SUBSTEP; };
        core::Vec2 last = core::SwordAt(VOLLEY, sword, timeOf(turnStart), MovingAim(timeOf(turnStart))).heading;
        f64 mostTurn = 0.0;
        for (u64 sample = turnStart + 1; sample <= turnEnd; ++sample) {
            const core::Vec2 heading = core::SwordAt(VOLLEY, sword, timeOf(sample), MovingAim(timeOf(sample))).heading;
            mostTurn = std::max(mostTurn, Turn(last, heading));
            last = heading;
        }
        CHECK(mostTurn < MOST_TURN);
    }
}

TEST_CASE("the path curves") {
    constexpr f64 LEAST_BEND = static_cast<f64>(SWORDS.sweep) / 4.0;
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        CAPTURE(sword);
        const u64 ring = core::LaunchTick(VOLLEY, sword) + SWORDS.outTicks;
        const core::Vec2 ringPoint = core::SwordAt(VOLLEY, sword, static_cast<f64>(ring), POINT).position;
        f64 farthest = 0.0;
        for (u32 tick = 0; tick <= SWORDS.flightTicks; ++tick) {
            const core::Vec2 position = core::SwordAt(VOLLEY, sword, static_cast<f64>(ring + tick), POINT).position;
            farthest = std::max(farthest, OffLine(position, ringPoint, POINT));
        }
        CHECK(farthest >= LEAST_BEND);
    }
}

TEST_CASE("the swords burst out in a ring") {
    constexpr f64 SLACK = 1e-3;
    constexpr f64 WIDEST_GAP = 3.0 * FULL_TURN / static_cast<f64>(SWORDS.count);
    std::vector<f64> angles{};
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        const u64 ring = core::LaunchTick(VOLLEY, sword) + SWORDS.outTicks;
        const core::Vec2 position = core::SwordAt(VOLLEY, sword, static_cast<f64>(ring), POINT).position;
        CHECK(std::abs(Distance(ORIGIN, position) - static_cast<f64>(SWORDS.ringRadius)) < SLACK);
        const core::Vec2 out = position - ORIGIN;
        angles.push_back(core::math::Atan2(static_cast<f64>(out.y), static_cast<f64>(out.x)));
    }
    std::ranges::sort(angles);
    f64 widestGap = angles.front() + FULL_TURN - angles.back();
    for (std::size_t index = 1; index < angles.size(); ++index) {
        widestGap = std::max(widestGap, angles[index] - angles[index - 1]);
    }
    CHECK(widestGap < WIDEST_GAP);
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

TEST_CASE("a sword shows from its launch and fades after its arrival, staying at the aim") {
    constexpr u32 SWORD = 499;
    constexpr f64 SLACK = 1e-6;
    constexpr f64 HALF_SHOWN = 0.5;
    const f64 launch = static_cast<f64>(core::LaunchTick(VOLLEY, SWORD));
    const f64 arrival = static_cast<f64>(core::ArrivalTick(VOLLEY, SWORD));
    const f64 fade = static_cast<f64>(SWORDS.fadeTicks);
    const core::SwordPose waiting = core::SwordAt(VOLLEY, SWORD, launch - SUBSTEP, POINT);
    CHECK(waiting.shown == 0.0F);
    CHECK(waiting.position.x == ORIGIN.x);
    CHECK(waiting.position.y == ORIGIN.y);
    CHECK(core::SwordAt(VOLLEY, SWORD, launch, POINT).shown == 1.0F);
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
            CHECK(position.x == targets.at(last).current.x);
            CHECK(position.y == targets.at(last).current.y);
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
    constexpr std::array<u32, 6> SAMPLED{ { 1, 5, 9, 13, 17, 21 } };
    // The chained swing is checked only if a sampled sword moves twice: first to 2, then on from it.
    const bool movesTwice = std::ranges::any_of(SAMPLED, [view](u32 sword) {
        return core::SwordTarget(VOLLEY, sword, FIRST_DEATH, view) == 2 &&
               core::SwordTarget(VOLLEY, sword, SECOND_DEATH, view) != 2;
    });
    REQUIRE(movesTwice);
    // Swords of target 1, which move once, or twice when they first moved to 2.
    for (const u32 sword : SAMPLED) {
        CAPTURE(sword);
        CheckSmooth(VOLLEY, sword, [view, sword](f64 tick) { return AimAt(sword, tick, view); });
    }
}

TEST_CASE("SwordTarget and SwordAim agree on the target at every moment") {
    constexpr u64 LATER_DEATH = 665; // a whole swing after the first, so each swing starts from a frozen point
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
            const core::Vec2 towards = targets.at(target).current;
            if (moved == 0) {
                CHECK(aim.x == towards.x);
                CHECK(aim.y == towards.y);
                continue;
            }
            // Swinging from the dead target's frozen point to the new one, eased, and over by the arrival.
            const core::Vec2 from = targets.at(movedFrom).current;
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
            CHECK(position.x == targets.at(struck).current.x);
            CHECK(position.y == targets.at(struck).current.y);
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
        CHECK(position.x == RIGHT.x);
        CHECK(position.y == RIGHT.y);
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
        CHECK(position.x == RIGHT.x);
        CHECK(position.y == RIGHT.y);
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
    CHECK(aim.x == doctest::Approx(BETWEEN.x));
    CHECK(aim.y == doctest::Approx(BETWEEN.y));
}

TEST_CASE("with no target tagged, every sword plunges at the point") {
    const std::array<core::VolleyTarget, 1> targets{ { { .previous = POINT, .current = POINT, .diedTick = START } } };
    for (const u32 sword : SAMPLED_SWORDS) {
        const u32 target = core::SwordTarget(VOLLEY, sword, START + SWORDS.flightTicks, targets);
        CHECK(target == 0);
        const core::Vec2 aim = core::SwordAim(VOLLEY, sword, START + SWORDS.flightTicks, 1.0F, targets);
        CHECK(aim.x == POINT.x);
        CHECK(aim.y == POINT.y);
    }
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

TEST_CASE("a cast volley's commit window is valid from the floor up") {
    core::Volley volley = VOLLEY;
    volley.shape.commitTicks = core::MIN_COMMIT_TICKS - 1;
    CHECK_FALSE(volley.Valid());
    volley.shape.commitTicks = core::MIN_COMMIT_TICKS;
    CHECK(volley.Valid());
}

} // namespace
