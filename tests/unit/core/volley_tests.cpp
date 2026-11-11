#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/math.hpp"
#include "core/volley.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 START = 600;
constexpr u64 SEED = 0x5eed'0123'4567'89abULL;
constexpr core::Vec2 ORIGIN{ .x = 100.0F, .y = 50.0F };
constexpr core::Vec2 POINT{ .x = 700.0F, .y = -100.0F };
// The shipped swords (assets/data/abilities.yaml) in ticks.
constexpr core::VolleyShape SWORDS{
    .count = 1'000,
    .ringRadius = 140.0F,
    .staggerTicks = 30,
    .outTicks = 12,
    .flightTicks = 66,
    .sweep = 420.0F,
    .fadeTicks = 18,
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

// Whether no step is longer than MOST_STEP times the median; all are 0 when the median is.
bool NoJump(std::vector<f64> steps) {
    if (steps.empty()) {
        return true;
    }
    const f64 longest = std::ranges::max(steps);
    std::ranges::nth_element(steps, steps.begin() + static_cast<std::ptrdiff_t>(steps.size() / 2));
    return longest <= MOST_STEP * steps[steps.size() / 2];
}

// Checks one sword's path from its launch through its fade, sampled between ticks, for jumps in position and heading,
// flying at `aimAt(tick)`.
template<typename AimAt>
void CheckSmooth(const core::Volley& volley, u32 sword, const AimAt& aimAt) {
    const u64 launch = core::LaunchTick(volley, sword);
    const u64 ring = launch + volley.shape.outTicks;
    const u64 arrival = core::ArrivalTick(volley, sword);
    const u64 samples = (arrival + volley.shape.fadeTicks - launch) * SUBSTEPS;
    std::array<std::vector<f64>, 3> steps{}; // out, flight, fade
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
        steps.at(phase).push_back(Distance(last.position, pose.position));
        last = pose;
    }
    CHECK(mostTurn < MOST_TURN);
    REQUIRE_FALSE(steps[1].empty());
    for (const std::vector<f64>& phase : steps) {
        CHECK(NoJump(phase));
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
        const u32 target = core::SwordTarget(sword, tick, targets);
        REQUIRE(target < targets.size());
        ++counts.at(target);
    }
    return counts;
}

constexpr u64 FIRST_DEATH = 650;  // mid-flight for every sword
constexpr u64 SECOND_DEATH = 665; // mid-flight, after the first death's swing (RETARGET_TICKS) ends

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
        CHECK(core::SwordTarget(sword, FIRST_DEATH - 1, view) == sword % 4);
        if (sword % 4 != 1) {
            CHECK(core::SwordTarget(sword, FIRST_DEATH, view) == sword % 4);
        }
    }
    // Target 1's 250 swords spread over the living 0, 2 and 3 as 84, 83 and 83.
    const std::array<u32, 4> counts = CountTargets(FIRST_DEATH, targets);
    CHECK(counts == std::array<u32, 4>{ { 334, 0, 333, 333 } });
}

TEST_CASE("a sword moved by one death keeps its new target through the next while that lives") {
    std::array<core::VolleyTarget, 4> targets = FourTargets();
    targets[1].diedTick = FIRST_DEATH;
    targets[2].diedTick = SECOND_DEATH;
    const std::span<const core::VolleyTarget> view = targets;
    constexpr u32 MOVED_TO_THREE = 9;
    CHECK(core::SwordTarget(MOVED_TO_THREE, FIRST_DEATH, view) == 3);
    CHECK(core::SwordTarget(MOVED_TO_THREE, SECOND_DEATH, view) == 3);
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        CAPTURE(sword);
        const u32 first = core::SwordTarget(sword, FIRST_DEATH, view);
        const u32 second = core::SwordTarget(sword, SECOND_DEATH, view);
        if (first != 2) {
            CHECK(second == first);
        }
        else {
            CHECK((second == 0 || second == 3));
        }
    }
    // Target 2's own 250 and the 83 it took from target 1 spread over 0 and 3.
    const std::array<u32, 4> counts = CountTargets(SECOND_DEATH, targets);
    CHECK(counts[0] + counts[3] == SWORDS.count);
    CHECK(counts[0] >= 334 + 166);
    CHECK(counts[3] >= 333 + 166);
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
        const u32 last = core::SwordTarget(sword, arrival, view);
        ++lasts.at(last);
        // It never goes back to a target that died before the one it ended on.
        CHECK(core::SwordTarget(sword, DEATHS[2] - 1, view) == last);
        for (const f64 tick : { static_cast<f64>(arrival), static_cast<f64>(arrival + SWORDS.fadeTicks) }) {
            const core::Vec2 aim = core::SwordAim(sword, tick, view, 1.0F);
            const core::Vec2 position = core::SwordAt(VOLLEY, sword, tick, aim).position;
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
    // Swords of target 1: 1 moves to 0, 5 to 2 then 0, 9 to 3, 13 to 0.
    for (const u32 sword : { 1U, 5U, 9U, 13U }) {
        CAPTURE(sword);
        CheckSmooth(VOLLEY, sword, [view, sword](f64 tick) { return core::SwordAim(sword, tick, view, 1.0F); });
    }
}

TEST_CASE("SwordTarget and SwordAim agree on the target at every moment") {
    std::array<core::VolleyTarget, 4> targets = FourTargets();
    targets[1].diedTick = FIRST_DEATH;
    targets[2].diedTick = SECOND_DEATH;
    const std::span<const core::VolleyTarget> view = targets;
    constexpr u64 SAMPLES = 120 * SUBSTEPS;
    // The longest way a swing can take here is the square's diagonal.
    const f64 diagonal = Distance(targets[1].current, targets[3].current);
    const f64 longestSwing = std::max(core::RETARGET_TICKS, diagonal / core::RETARGET_SPEED);
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        CAPTURE(sword);
        u64 moved = 0; // the tick it last moved on from a dead target
        for (u64 sample = 0; sample <= SAMPLES; ++sample) {
            const u64 tick = START + (sample / SUBSTEPS);
            const f64 time = static_cast<f64>(START) + (static_cast<f64>(sample) * SUBSTEP);
            const u32 target = core::SwordTarget(sword, tick, view);
            if (tick > START && target != core::SwordTarget(sword, tick - 1, view)) {
                moved = tick;
            }
            const core::Vec2 aim = core::SwordAim(sword, time, view, 1.0F);
            if (moved == 0 || time >= static_cast<f64>(moved) + longestSwing) {
                CHECK(aim.x == targets.at(target).current.x);
                CHECK(aim.y == targets.at(target).current.y);
            }
            else {
                // Swinging from the dead target's frozen point towards its new one.
                CHECK(Distance(aim, targets.at(target).current) <= diagonal);
            }
        }
    }
}

TEST_CASE("a living target's aim is interpolated by the frame's alpha") {
    constexpr core::Vec2 BEFORE{ .x = 600.0F, .y = 0.0F };
    constexpr core::Vec2 AFTER{ .x = 640.0F, .y = 20.0F };
    constexpr f32 ALPHA = 0.25F;
    const std::array<core::VolleyTarget, 1> targets{
        { { .entity = core::StableId{ 1 }, .previous = BEFORE, .current = AFTER } }
    };
    constexpr core::Vec2 BETWEEN{ .x = 610.0F, .y = 5.0F }; // a quarter of the way
    const core::Vec2 aim = core::SwordAim(0, static_cast<f64>(START) + static_cast<f64>(ALPHA), targets, ALPHA);
    CHECK(aim.x == doctest::Approx(BETWEEN.x));
    CHECK(aim.y == doctest::Approx(BETWEEN.y));
}

TEST_CASE("with no target tagged, every sword plunges at the point") {
    const std::array<core::VolleyTarget, 1> targets{ { { .previous = POINT, .current = POINT, .diedTick = START } } };
    for (const u32 sword : SAMPLED_SWORDS) {
        CHECK(core::SwordTarget(sword, START + SWORDS.flightTicks, targets) == 0);
        const core::Vec2 aim = core::SwordAim(sword, static_cast<f64>(START + SWORDS.flightTicks), targets, 1.0F);
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
}

} // namespace
