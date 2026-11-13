// The arena's showcase seals as drawn (the phase 6 chunk 4b spec, "The showcase"): the arena places them, seals.yaml
// gives their looks and gfx lays them out, so only a test that has all three can check where they reach.
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/game_data.hpp"
#include "core/jobs.hpp"
#include "core/seal.hpp"
#include "core/seals.hpp"
#include "core/tick_clock.hpp"
#include "core/ui_colours.hpp"
#include "game/arena.hpp"
#include "gfx/seal_draw.hpp"
#include "support/manifest.hpp"
#include "support/seals.hpp"

#include <doctest/doctest.h>

namespace {

// Where a seal reaches in the world: a box round everything it draws.
struct Box {
    core::Vec2 min{ .x = std::numeric_limits<f32>::max(), .y = std::numeric_limits<f32>::max() };
    core::Vec2 max{ .x = std::numeric_limits<f32>::lowest(), .y = std::numeric_limits<f32>::lowest() };

    void Add(core::Vec2 point) {
        min = { .x = std::min(min.x, point.x), .y = std::min(min.y, point.y) };
        max = { .x = std::max(max.x, point.x), .y = std::max(max.y, point.y) };
    }

    void Add(core::Vec2 point, f32 reach) {
        Add(point - core::Vec2{ .x = reach, .y = reach });
        Add(point + core::Vec2{ .x = reach, .y = reach });
    }

    [[nodiscard]] bool Overlaps(const Box& other) const {
        return min.x < other.max.x && other.min.x < max.x && min.y < other.max.y && other.min.y < max.y;
    }
};

// Where `light`'s particles can reach: its disc, out to twice its size where its shards fade out, its pillars their length
// from its plane, its sparks their rise over a life; each grown by its particle's size.
void AddLight(Box& box, const gfx::SealGpuParticles& light) {
    const auto disc = [&light](f32 reach, core::Vec2 shift, f32 grow, Box& into) {
        for (const f32 across : { -reach, reach }) {
            for (const f32 depth : { -reach, reach }) {
                into.Add(light.centre + (light.axisX * across) + (light.axisDepth * depth) + shift, grow);
            }
        }
    };
    constexpr f32 SHARDS_REACH = 2.0F; // born on the disc, they drift one seal size out over their cycle
    if (light.pillars > 0) {
        disc(1.0F, { .y = light.pillarFall * light.pillarHeight }, light.pillarWidth, box);
    }
    if (light.sparks > 0) {
        disc(1.0F, { .y = -light.sparkRise * light.sparkLife }, light.sparkSize, box);
    }
    if (light.shards > 0) {
        disc(SHARDS_REACH, {}, light.shardSize, box);
    }
}

// Where `seal` reaches as drawn by `looks` at `seconds`, its satellites with it: its washes', shadows' and bands' quads as
// laid out, and its light's reach.
Box Footprint(const core::Seal& seal, const core::Seals& looks, const gfx::SealPack& pack, f64 seconds) {
    gfx::SealDrawing drawn;
    const auto tick = static_cast<u64>(seconds * static_cast<f64>(core::TICKS_PER_SECOND));
    gfx::DrawSeals(std::span{ &seal, 1 }, looks, pack, core::UiColours{}, seconds, tick, true, drawn);
    Box box;
    for (const std::vector<gfx::SealVertex>* quads :
         { &drawn.groundWash, &drawn.groundLines, &drawn.aboveWash, &drawn.aboveLines }) {
        for (const gfx::SealVertex& vertex : *quads) {
            box.Add(vertex.position);
        }
    }
    for (const std::vector<gfx::SealGpuParticles>* lights : { &drawn.groundParticles, &drawn.aboveParticles }) {
        for (const gfx::SealGpuParticles& light : *lights) {
            AddLight(box, light);
        }
    }
    return box;
}

// Where `seal` reaches over half a minute (a turn of its slowest orbiting satellite), from ten seconds on, when every look
// has appeared whole.
Box FootprintOverTime(const core::Seal& seal, const core::Seals& looks, const gfx::SealPack& pack) {
    constexpr f64 FROM = 10.0;
    constexpr f64 STEP = 2.5;
    constexpr u32 STEPS = 12;
    Box box;
    for (u32 step = 0; step <= STEPS; ++step) {
        const Box then = Footprint(seal, looks, pack, FROM + (STEP * static_cast<f64>(step)));
        box.Add(then.min);
        box.Add(then.max);
    }
    return box;
}

TEST_CASE("the showcase seals keep apart and clear of the swords' seal at the player's start, so none hides another") {
    core::Jobs jobs{ 1 };
    game::Arena arena(test::TestManifest());
    core::Snapshot snapshot;
    arena.Publish(snapshot, jobs);
    REQUIRE(snapshot.follow.has_value());
    const core::Seals looks = test::ShippedSeals();
    const gfx::SealPack pack = gfx::PackSeals(looks, *test::TestManifest());

    // The swords' seal as a cast at the player's start raises it, in the game data's size.
    const std::optional<core::SealCast> cast = game::BuiltInData().abilities.Of(core::AbilityKind::FlyingSwords).seal;
    REQUIRE(cast.has_value());
    const core::SealCast swords = cast.value_or(core::SealCast{});
    const core::Seal start{ .key = swords.key,
                            .point = snapshot.follow.value_or(core::Follow{}).current,
                            .size = swords.size,
                            .endTick = core::NEVER_ENDS,
                            .hasElement = true };
    const Box startArea = FootprintOverTime(start, looks, pack);

    std::vector<Box> boxes;
    boxes.reserve(snapshot.seals.size());
    for (const core::Seal& seal : snapshot.seals) {
        boxes.push_back(FootprintOverTime(seal, looks, pack));
    }
    REQUIRE(boxes.size() > 1);
    std::size_t overlaps = 0;
    std::string report = std::format(
        "the swords' seal at the start reaches ({}, {}) to ({}, {})\n",
        startArea.min.x,
        startArea.min.y,
        startArea.max.x,
        startArea.max.y
    );
    for (std::size_t first = 0; first < boxes.size(); ++first) {
        const Box& box = boxes[first];
        report +=
            std::format("showcase seal {} reaches ({}, {}) to ({}, {})\n", first, box.min.x, box.min.y, box.max.x, box.max.y);
        if (box.Overlaps(startArea)) {
            report += std::format("  it reaches the player's start\n");
            ++overlaps;
        }
        for (std::size_t second = first + 1; second < boxes.size(); ++second) {
            if (box.Overlaps(boxes[second])) {
                report += std::format("  it overlaps showcase seal {}\n", second);
                ++overlaps;
            }
        }
    }
    INFO(report);
    CHECK(overlaps == 0);
}

} // namespace
