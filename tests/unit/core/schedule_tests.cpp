#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/schedule.hpp"

#include <doctest/doctest.h>

namespace {

TEST_CASE("a rate runs on every k-th tick at its offset") {
    constexpr core::Rate EVERY_FOURTH{ .every = 4, .offset = 1 };
    constexpr u64 TICKS = 12;
    std::vector<u64> ran;
    for (u64 tick = 1; tick <= TICKS; ++tick) {
        if (core::RunsOn(EVERY_FOURTH, tick)) {
            ran.push_back(tick);
        }
    }
    CHECK(ran == std::vector<u64>{ 1, 5, 9 });
}

TEST_CASE("the default rate runs on every tick") {
    constexpr u64 TICKS = 5;
    for (u64 tick = 1; tick <= TICKS; ++tick) {
        CHECK(core::RunsOn(core::Rate{}, tick));
    }
}

TEST_CASE("a time slice processes each entity exactly once every k ticks") {
    constexpr u32 EVERY = 6;
    constexpr u64 ENTITIES = 100;
    for (u64 id = 1; id <= ENTITIES; ++id) {
        u32 processed = 0;
        for (u64 tick = 1; tick <= EVERY; ++tick) {
            processed += core::InSlice(EVERY, tick, core::StableId{ id }) ? 1U : 0U;
        }
        CHECK(processed == 1);
    }
}

TEST_CASE("a time slice spreads the entities evenly over its ticks") {
    constexpr u32 EVERY = 6;
    constexpr u64 ENTITIES = 6'000;
    constexpr u32 EVEN = ENTITIES / EVERY;
    constexpr u32 SPREAD = EVEN / 10; // within 10%
    std::array<u32, EVERY> perTick{};
    for (u64 id = 1; id <= ENTITIES; ++id) {
        for (u32 tick = 0; tick < EVERY; ++tick) {
            perTick.at(tick) += core::InSlice(EVERY, tick, core::StableId{ id }) ? 1U : 0U;
        }
    }
    for (const u32 count : perTick) {
        CHECK(count > EVEN - SPREAD);
        CHECK(count < EVEN + SPREAD);
    }
}

} // namespace
