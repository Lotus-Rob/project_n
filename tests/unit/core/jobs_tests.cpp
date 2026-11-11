#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/jobs.hpp"
#include "core/math.hpp"
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

TEST_CASE("every parallel-for chunk runs in the simulation's floating-point environment") {
    constexpr u32 CHUNKS = 10'000;
    // A new thread may inherit its creator's environment, which would hide a worker that never sets its own.
    std::fesetround(FE_UPWARD);
    core::Jobs jobs{ MANY_WORKERS };
    core::math::SetFloatingPointEnvironment();

    const std::thread::id forker = std::this_thread::get_id();
    std::atomic<bool> workerRan = false;
    std::atomic<u32> chunksWithoutIt = 0;
    jobs.ParallelFor(CHUNKS, [&](u32 /*chunk*/) {
        if (!core::math::FloatingPointEnvironmentIsSet()) {
            chunksWithoutIt.fetch_add(1, std::memory_order_relaxed);
        }
        if (std::this_thread::get_id() != forker) {
            workerRan.store(true, std::memory_order_relaxed);
        }
        // The forking thread could otherwise finish every chunk before a worker wakes.
        while (!workerRan.load(std::memory_order_relaxed)) {
            std::this_thread::yield();
        }
    });
    CHECK(chunksWithoutIt.load() == 0);
}

TEST_CASE("a background job runs once, and waiting for it sees what it did") {
    core::Jobs jobs{ MANY_WORKERS };
    u32 runs = 0;
    jobs.Background([&runs] { ++runs; });
    jobs.WaitForBackground();
    CHECK(runs == 1);
}

TEST_CASE("starting a background job waits for the last one first") {
    core::Jobs jobs{ MANY_WORKERS };
    std::vector<u32> order;
    jobs.Background([&order] { order.push_back(1); });
    jobs.Background([&order] { order.push_back(2); });
    jobs.WaitForBackground();
    CHECK(order == std::vector<u32>{ 1, 2 });
}

TEST_CASE("waiting with no background job returns at once") {
    core::Jobs jobs{ 1 };
    jobs.WaitForBackground();
    CHECK(jobs.Workers() == 1);
}

TEST_CASE("started chunks all run, and Done turns true once they have") {
    constexpr u32 CHUNKS = 1'000;
    core::Jobs jobs{ MANY_WORKERS };
    std::vector<u32> runs(CHUNKS);
    const core::Jobs::Pending pending = jobs.Start(CHUNKS, [&runs](u32 chunk) { ++runs[chunk]; });
    while (!pending.Done()) {
        std::this_thread::yield();
    }
    CHECK(std::ranges::all_of(runs, [](u32 count) { return count == 1; }));
}

TEST_CASE("starting no chunks is done at once") {
    core::Jobs jobs{ 1 };
    const core::Jobs::Pending pending = jobs.Start(0, [](u32 /*chunk*/) {});
    CHECK(pending.Done());
}

TEST_CASE("a parallel-for returns while started low-priority work is still blocked") {
    core::Jobs jobs{ 1 };
    std::atomic<bool> release = false;
    const core::Jobs::Pending pending = jobs.Start(1, [&release](u32 /*chunk*/) {
        while (!release.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    });
    std::atomic<u32> ran = 0;
    jobs.ParallelFor(4, [&ran](u32 /*chunk*/) { ran.fetch_add(1, std::memory_order_relaxed); });
    CHECK(ran.load() == 4);
    CHECK_FALSE(pending.Done());
    release.store(true, std::memory_order_release);
}

TEST_CASE("a registered thread forks, joins and runs background jobs") {
    constexpr u32 CHUNKS = 1'000;
    core::Jobs jobs{ MANY_WORKERS, /*externalThreads=*/1 };
    std::vector<u32> runs(CHUNKS);
    u32 background = 0;
    std::thread{ [&] {
        const core::Jobs::ThreadRegistration registration{ jobs };
        jobs.ParallelFor(CHUNKS, [&runs](u32 chunk) { ++runs[chunk]; });
        jobs.Background([&background] { ++background; });
        jobs.WaitForBackground();
    } }.join();
    CHECK(std::ranges::all_of(runs, [](u32 count) { return count == 1; }));
    CHECK(background == 1);
}

TEST_CASE("a pending job found done runs no other work while it goes") {
    // Done() once read a counter set before enkiTS marked the task complete, and the destructor's wait then ran the
    // next low-priority task on this thread. The window is a few instructions wide: before the fix, these rounds lost it
    // 18 to 103 times a run (six runs on a many-core machine, about 0.15 s each), so a run misses it about once in e^18;
    // after it, none can. Both spins yield, so a runner with one core (where the race can't show) isn't held up.
    constexpr u32 ROUNDS = 150'000;
    core::Jobs jobs{ 1 };
    const std::thread::id self = std::this_thread::get_id();
    u32 intrusions = 0;
    for (u32 round = 0; round < ROUNDS; ++round) {
        std::atomic<bool> going = false;
        std::atomic<bool> intruded = false;
        std::optional<core::Jobs::Pending> first{ jobs.Start(1, [](u32 /*chunk*/) {}) };
        const core::Jobs::Pending second =
            jobs.Start(1, [&](u32 /*chunk*/) { intruded.store(going.load() && std::this_thread::get_id() == self); });
        while (!first->Done()) {
            std::this_thread::yield();
        }
        going.store(true);
        first.reset();
        going.store(false);
        while (!second.Done()) {
            std::this_thread::yield();
        }
        intrusions += intruded.load() ? 1 : 0;
    }
    CHECK(intrusions == 0);
}

} // namespace
