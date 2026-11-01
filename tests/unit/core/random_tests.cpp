#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/random.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 SEED = 42;
constexpr u64 STATE = 7;
constexpr u64 ENTITY = 1'000;
constexpr u32 PURPOSE = 3;

TEST_CASE("the same key gives the same number, on every machine and in every release") {
    // Saves and replays depend on this value: changing the algorithm breaks them.
    CHECK(core::Random(SEED, STATE, ENTITY, PURPOSE, 0) == 0x643a'482c'e1d6'3029ULL);
}

TEST_CASE("every part of the key changes the number") {
    const u64 base = core::Random(SEED, STATE, ENTITY, PURPOSE, 0);
    CHECK(core::Random(SEED + 1, STATE, ENTITY, PURPOSE, 0) != base);
    CHECK(core::Random(SEED, STATE + 1, ENTITY, PURPOSE, 0) != base);
    CHECK(core::Random(SEED, STATE, ENTITY + 1, PURPOSE, 0) != base);
    CHECK(core::Random(SEED, STATE, ENTITY, PURPOSE + 1, 0) != base);
    CHECK(core::Random(SEED, STATE, ENTITY, PURPOSE, 1) != base);
}

TEST_CASE("swapping two parts of the key changes the number") {
    constexpr u64 FIRST = 5;
    constexpr u64 SECOND = 9;
    CHECK(core::Random(SEED, FIRST, SECOND, PURPOSE, 0) != core::Random(SEED, SECOND, FIRST, PURPOSE, 0));
}

TEST_CASE("draws spread evenly over a range") {
    constexpr u32 BUCKETS = 10;
    constexpr u32 DRAWS = 100'000;
    constexpr u32 EXPECTED = DRAWS / BUCKETS;
    constexpr u32 TOLERANCE = EXPECTED / 20; // 5%
    std::array<u32, BUCKETS> counts{};
    for (u32 draw = 0; draw < DRAWS; ++draw) {
        const u64 random = core::Random(SEED, STATE, ENTITY, PURPOSE, draw);
        const u32 bucket = core::ToBelow(random, BUCKETS);
        REQUIRE(bucket < BUCKETS);
        ++counts[bucket];
    }
    for (const u32 count : counts) {
        CHECK(count > EXPECTED - TOLERANCE);
        CHECK(count < EXPECTED + TOLERANCE);
    }
}

TEST_CASE("the unit value spans [0, 1)") {
    CHECK(core::ToUnit(0) == 0.0);
    CHECK(core::ToUnit(~u64{ 0 }) < 1.0);
}

TEST_CASE("unit values of many draws lie in [0, 1)") {
    constexpr u32 DRAWS = 10'000;
    for (u32 draw = 0; draw < DRAWS; ++draw) {
        const f64 unit = core::ToUnit(core::Random(SEED, STATE, ENTITY, PURPOSE, draw));
        REQUIRE(unit >= 0.0);
        REQUIRE(unit < 1.0);
    }
}

} // namespace
