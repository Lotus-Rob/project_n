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

// Checks one sword's path from its launch to its arrival, sampled between ticks, for jumps in position and heading.
void CheckSmooth(u32 sword, core::Vec2 (*aimAt)(f64)) {
    constexpr f64 MOST_TURN = 0.25; // radians between samples
    constexpr f64 MOST_STEP = 3.0;  // times the median step
    const u64 launch = core::LaunchTick(VOLLEY, sword);
    const u64 samples = (core::ArrivalTick(VOLLEY, sword) - launch) * SUBSTEPS;
    std::vector<f64> steps{};
    core::SwordPose last = core::SwordAt(VOLLEY, sword, static_cast<f64>(launch), aimAt(static_cast<f64>(launch)));
    f64 mostTurn = 0.0;
    for (u64 sample = 1; sample <= samples; ++sample) {
        const f64 tick = static_cast<f64>(launch) + (static_cast<f64>(sample) * SUBSTEP);
        const core::SwordPose pose = core::SwordAt(VOLLEY, sword, tick, aimAt(tick));
        REQUIRE(core::IsFinite(pose.position));
        REQUIRE(IsUnit(pose.heading));
        mostTurn = std::max(mostTurn, Turn(last.heading, pose.heading));
        steps.push_back(Distance(last.position, pose.position));
        last = pose;
    }
    CHECK(mostTurn < MOST_TURN);
    REQUIRE_FALSE(steps.empty());
    const f64 longest = std::ranges::max(steps);
    std::ranges::nth_element(steps, steps.begin() + static_cast<std::ptrdiff_t>(steps.size() / 2));
    const f64 median = steps[steps.size() / 2];
    CHECK(longest <= MOST_STEP * median);
}

std::array<u32, 4> CountTargets(std::span<const core::VolleyTarget> targets) {
    std::array<u32, 4> counts{};
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        const u32 target = core::SwordTarget(sword, targets);
        REQUIRE(target < targets.size());
        ++counts.at(target);
    }
    return counts;
}

std::array<core::VolleyTarget, 4> FourTargets() {
    std::array<core::VolleyTarget, 4> targets{};
    u64 entity = 1;
    for (core::VolleyTarget& target : targets) {
        target = { .entity = core::StableId{ entity++ }, .previous = POINT, .current = POINT, .alive = true };
    }
    return targets;
}

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
    for (const u32 sword : SAMPLED_SWORDS) {
        CAPTURE(sword);
        CheckSmooth(sword, StillAim);
        CheckSmooth(sword, MovingAim);
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
    const std::array<u32, 4> counts = CountTargets(targets);
    CHECK(counts == std::array<u32, 4>{ { 250, 250, 250, 250 } });
}

TEST_CASE("a dead target's swords move to the living, the others stay") {
    std::array<core::VolleyTarget, 4> targets = FourTargets();
    targets[1].alive = false;
    const std::span<const core::VolleyTarget> view = targets;
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        if (sword % 4 != 1) {
            CHECK(core::SwordTarget(sword, view) == sword % 4);
        }
    }
    // Target 1's 250 swords spread over the living 0, 2 and 3 as 84, 83 and 83.
    const std::array<u32, 4> counts = CountTargets(targets);
    CHECK(counts == std::array<u32, 4>{ { 334, 0, 333, 333 } });
}

TEST_CASE("with every target dead, each sword keeps its primary") {
    std::array<core::VolleyTarget, 4> targets = FourTargets();
    for (core::VolleyTarget& target : targets) {
        target.alive = false;
    }
    const std::span<const core::VolleyTarget> view = targets;
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        CHECK(core::SwordTarget(sword, view) == sword % 4);
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
