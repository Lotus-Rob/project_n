#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/draw_list.hpp"
#include "core/random.hpp"

#include <doctest/doctest.h>

namespace {

using core::Layer;
using core::Pipeline;

// An instance told apart from others by its x.
core::Instance Marked(f32 mark) {
    return { .current = { .x = mark, .y = 0.0F } };
}

std::vector<f32> Marks(const core::Snapshot& snapshot) {
    std::vector<f32> marks;
    marks.reserve(snapshot.instances.size());
    for (const core::Instance& instance : snapshot.instances) {
        marks.push_back(instance.current.x);
    }
    return marks;
}

TEST_CASE("a sort key orders by layer, then depth, then pipeline") {
    constexpr u32 DEEP = 1'000'000;
    CHECK(core::MakeSortKey(Layer::Decals, DEEP, Pipeline::Additive) < core::MakeSortKey(Layer::World, 0, Pipeline::Normal));
    CHECK(core::MakeSortKey(Layer::World, 1, Pipeline::Normal) < core::MakeSortKey(Layer::World, 2, Pipeline::Normal));
    CHECK(core::MakeSortKey(Layer::World, 1, Pipeline::Additive) < core::MakeSortKey(Layer::World, 2, Pipeline::Normal));
    CHECK(core::MakeSortKey(Layer::World, 1, Pipeline::Normal) < core::MakeSortKey(Layer::World, 1, Pipeline::Additive));
    CHECK(
        core::MakeSortKey(Layer::World, std::numeric_limits<u32>::max(), Pipeline::Additive) <
        core::MakeSortKey(Layer::Effects, 0, Pipeline::Normal)
    );
    CHECK(core::PipelineOf(core::MakeSortKey(Layer::Effects, DEEP, Pipeline::Additive)) == Pipeline::Additive);
    CHECK(core::PipelineOf(core::MakeSortKey(Layer::Effects, DEEP, Pipeline::Normal)) == Pipeline::Normal);
}

TEST_CASE("depth grows down the view in sixteenths of a unit, and clamps at both ends") {
    constexpr f32 TOP = -100.0F;
    constexpr f32 ABOVE = -150.0F;
    constexpr f32 ONE_BELOW = -99.0F;
    constexpr f32 A_STEP_FURTHER = -98.9375F; // one sixteenth more
    constexpr u32 STEPS_PER_UNIT = 16;
    constexpr f32 FAR_BELOW = 1e30F;
    CHECK(core::QuantiseDepth(TOP, TOP) == 0);
    CHECK(core::QuantiseDepth(ABOVE, TOP) == 0);
    CHECK(core::QuantiseDepth(ONE_BELOW, TOP) == STEPS_PER_UNIT);
    CHECK(core::QuantiseDepth(A_STEP_FURTHER, TOP) == STEPS_PER_UNIT + 1);
    CHECK(core::QuantiseDepth(FAR_BELOW, TOP) == std::numeric_limits<u32>::max());
    CHECK(core::QuantiseDepth(std::numeric_limits<f32>::quiet_NaN(), TOP) == 0);
}

TEST_CASE("the radix sort matches a stable sort") {
    constexpr std::size_t COUNT = 5'000;
    constexpr u64 SEED = 42;
    constexpr u32 PURPOSE = 1;
    constexpr u64 FEW_DEPTHS = 64; // many equal keys, so stability shows
    constexpr std::array<Layer, 4> LAYERS{ { Layer::Ground, Layer::Decals, Layer::World, Layer::Effects } };
    std::vector<core::SortEntry> entries;
    for (u32 i = 0; i < COUNT; ++i) {
        const u64 random = core::Random(SEED, 0, 0, PURPOSE, i);
        const Layer layer = LAYERS.at(random % LAYERS.size());
        const auto depth = static_cast<u32>((random >> 8U) % FEW_DEPTHS);
        const Pipeline pipeline = ((random >> 32U) & 1U) == 0 ? Pipeline::Normal : Pipeline::Additive;
        entries.push_back({ .key = core::MakeSortKey(layer, depth, pipeline), .index = i });
    }
    std::vector<core::SortEntry> expected = entries;
    std::ranges::stable_sort(expected, {}, &core::SortEntry::key);
    std::vector<core::SortEntry> scratch;
    core::RadixSort(entries, scratch);
    REQUIRE(entries.size() == expected.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        CHECK(entries[i].key == expected[i].key);
        CHECK(entries[i].index == expected[i].index);
    }
}

TEST_CASE("the radix sort handles no entries and keys that differ only in their top byte") {
    std::vector<core::SortEntry> scratch;
    std::vector<core::SortEntry> none;
    core::RadixSort(none, scratch);
    CHECK(none.empty());
    std::vector<core::SortEntry> entries{
        { .key = core::MakeSortKey(Layer::Effects, 0, Pipeline::Normal), .index = 0 },
        { .key = core::MakeSortKey(Layer::Ground, 0, Pipeline::Normal), .index = 1 },
    };
    core::RadixSort(entries, scratch);
    CHECK(entries[0].index == 1);
    CHECK(entries[1].index == 0);
}

TEST_CASE("batches cover the sorted entries, one per run of a pipeline") {
    const std::array sorted{
        core::SortEntry{ .key = core::MakeSortKey(Layer::Ground, 0, Pipeline::Normal) },
        core::SortEntry{ .key = core::MakeSortKey(Layer::World, 3, Pipeline::Normal) },
        core::SortEntry{ .key = core::MakeSortKey(Layer::Effects, 0, Pipeline::Additive) },
        core::SortEntry{ .key = core::MakeSortKey(Layer::Effects, 0, Pipeline::Additive) },
    };
    std::vector<core::Batch> batches;
    core::AppendBatches(sorted, batches);
    REQUIRE(batches.size() == 2);
    CHECK(batches[0].first == 0);
    CHECK(batches[0].count == 2);
    CHECK(batches[0].pipeline == Pipeline::Normal);
    CHECK(batches[1].first == 2);
    CHECK(batches[1].count == 2);
    CHECK(batches[1].pipeline == Pipeline::Additive);
    batches.clear();
    core::AppendBatches({}, batches);
    CHECK(batches.empty());
}

struct Added {
    Layer layer = Layer::World;
    Pipeline pipeline = Pipeline::Normal;
    f32 mark = 0.0F; // where it should end up, counting from 1
    f32 footY = 0.0F;
};

// Out of order on purpose; two world sprites share a foot y, so the first added must stay first.
constexpr std::array ADDED{
    Added{ .layer = Layer::Effects, .pipeline = Pipeline::Additive, .mark = 7.0F },
    Added{ .layer = Layer::World, .mark = 5.0F, .footY = 900.0F },
    Added{ .layer = Layer::Decals, .mark = 2.0F },
    Added{ .layer = Layer::World, .mark = 3.0F, .footY = 100.0F },
    Added{ .layer = Layer::World, .mark = 4.0F, .footY = 100.0F },
    Added{ .layer = Layer::Ground, .mark = 1.0F },
    Added{ .layer = Layer::Effects, .pipeline = Pipeline::Additive, .mark = 9.0F },
    Added{ .layer = Layer::Decals, .mark = 2.5F },
    Added{ .layer = Layer::World, .pipeline = Pipeline::Additive, .mark = 6.0F, .footY = 950.0F },
};
constexpr std::array<f32, 9> PUBLISHED{ { 1.0F, 2.0F, 2.5F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 9.0F } };
constexpr u32 NORMAL_RUN = 6; // ground, decals and the normal world sprites; then the additive ones

TEST_CASE("a draw list publishes by layer, the world by foot y, the rest in the order added") {
    core::DrawList list;
    list.Begin(0.0F);
    for (const Added& added : ADDED) {
        list.Add(added.layer, added.pipeline, Marked(added.mark), added.footY);
    }
    core::Snapshot snapshot;
    list.Publish(snapshot);
    CHECK(std::ranges::equal(Marks(snapshot), PUBLISHED));
    REQUIRE(snapshot.batches.size() == 2);
    CHECK(snapshot.batches[0].first == 0);
    CHECK(snapshot.batches[0].count == NORMAL_RUN);
    CHECK(snapshot.batches[1].pipeline == Pipeline::Additive);
    CHECK(snapshot.batches[1].first == NORMAL_RUN);
    CHECK(snapshot.batches[1].count == ADDED.size() - NORMAL_RUN);
}

TEST_CASE("a draw list starts empty at each Begin and appends after what the snapshot holds") {
    core::DrawList list;
    list.Begin(0.0F);
    constexpr f32 DROPPED = 2.0F;
    list.Add(Layer::World, Pipeline::Normal, Marked(DROPPED));
    list.Begin(0.0F);
    list.Add(Layer::World, Pipeline::Additive, Marked(1.0F));
    core::Snapshot snapshot;
    snapshot.instances.push_back(Marked(0.0F));
    snapshot.batches.push_back({ .first = 0, .count = 1, .pipeline = Pipeline::Normal });
    list.Publish(snapshot);
    CHECK(Marks(snapshot) == std::vector{ 0.0F, 1.0F });
    REQUIRE(snapshot.batches.size() == 2);
    CHECK(snapshot.batches[1].first == 1);
    CHECK(snapshot.batches[1].count == 1);
}

} // namespace
