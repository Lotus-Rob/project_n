#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/jobs.hpp"
#include "core/random.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 MANY_WORKERS = 7;

TEST_CASE("chunks cover the items, the last one short") {
    constexpr u32 CHUNK = 256;
    CHECK(core::ChunkCount(0, CHUNK) == 0);
    CHECK(core::ChunkCount(1, CHUNK) == 1);
    CHECK(core::ChunkCount(CHUNK, CHUNK) == 1);
    CHECK(core::ChunkCount(CHUNK + 1, CHUNK) == 2);
}

TEST_CASE("a parallel-for runs every chunk exactly once before it returns") {
    constexpr u32 CHUNKS = 10'000;
    core::Jobs jobs{ MANY_WORKERS };
    std::vector<std::atomic<u32>> runs(CHUNKS);
    jobs.ParallelFor(CHUNKS, [&runs](u32 chunk) { runs[chunk].fetch_add(1, std::memory_order_relaxed); });
    CHECK(std::ranges::all_of(runs, [](const std::atomic<u32>& count) { return count.load() == 1; }));
}

TEST_CASE("a parallel-for of no chunks calls nothing") {
    core::Jobs jobs{ 1 };
    bool called = false;
    jobs.ParallelFor(0, [&called](u32 /*chunk*/) { called = true; });
    CHECK_FALSE(called);
}

TEST_CASE("the job system runs at least one worker") {
    const core::Jobs jobs{ 0 };
    CHECK(jobs.Workers() == 1);
    CHECK(core::Jobs::DefaultWorkers() >= 1);
}

// A floating-point sum over fixed chunks, partials added in chunk order: the same bits whatever
// the worker count, because no sum's order depends on scheduling.
f32 ChunkedSum(core::Jobs& jobs, const std::vector<f32>& values) {
    constexpr u32 CHUNK = 1'000;
    const u32 chunks = core::ChunkCount(values.size(), CHUNK);
    std::vector<f32> partials(chunks);
    jobs.ParallelFor(chunks, [&](u32 chunk) {
        const std::size_t first = std::size_t{ chunk } * CHUNK;
        const std::size_t last = std::min(values.size(), first + CHUNK);
        f32 sum = 0.0F;
        for (std::size_t i = first; i < last; ++i) {
            sum += values[i];
        }
        partials[chunk] = sum;
    });
    return std::accumulate(partials.begin(), partials.end(), 0.0F);
}

TEST_CASE("results are identical with one worker and with many") {
    constexpr u32 COUNT = 100'000;
    constexpr u64 SEED = 7;
    constexpr u32 PURPOSE = 3;
    std::vector<f32> values;
    values.reserve(COUNT);
    for (u32 i = 0; i < COUNT; ++i) {
        // Mixed magnitudes, so the order of additions changes the rounding.
        values.push_back(static_cast<f32>(core::ToUnit(core::Random(SEED, 0, 0, PURPOSE, i))) * static_cast<f32>(i));
    }
    // One job system per thread at a time (core::Jobs).
    const f32 alone = [&values] {
        core::Jobs one{ 1 };
        return ChunkedSum(one, values);
    }();
    core::Jobs many{ MANY_WORKERS };
    CHECK(std::bit_cast<u32>(ChunkedSum(many, values)) == std::bit_cast<u32>(alone));
}

} // namespace
