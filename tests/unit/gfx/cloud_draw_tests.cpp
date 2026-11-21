#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/instances.hpp"
#include "core/manifest.hpp"
#include "core/rect.hpp"
#include "core/view_limits.hpp"
#include "core/world_look.hpp"
#include "core/world_map.hpp"
#include "data/strings.hpp"
#include "data/world_map.hpp"
#include "gfx/cloud_draw.hpp"
#include "gfx/fog.hpp"
#include "platform/paths.hpp"
#include "support/manifest.hpp"

#include <doctest/doctest.h>

namespace {

using core::atlas::SpriteId;

constexpr f32 TILE = 256.0F;
constexpr f32 HALF = 0.5F;
constexpr u8 FULL = 255;
constexpr u32 SIDE = 16;
constexpr u32 HIGH_HALF = 16;
constexpr u32 ALPHA_SHIFT = 24;
constexpr u32 BYTE = 0xFFU;
// Half precision keeps a cloud's size to within half a world unit at these sizes.
constexpr f32 SIZE_SLACK = 1.0F;
constexpr std::size_t LEVELS = 3;

/// A map's eased explored values, a tile at a time: every tile unexplored until told otherwise.
struct Field {
    u32 columns = SIDE;
    u32 rows = SIDE;
    std::vector<u8> values;

    Field(u32 across, u32 down, u8 value = 0)
      : columns(across)
      , rows(down)
      , values(std::size_t{ across } * down * 2, 0) {
        for (std::size_t tile = 0; tile < std::size_t{ across } * down; ++tile) {
            values.at(tile * 2) = value;
        }
    }

    void Set(u32 column, u32 row, u8 value) {
        values.at(((std::size_t{ row } * columns) + column) * 2) = value;
    }

    [[nodiscard]] u8 At(u32 column, u32 row) const {
        return values.at(((std::size_t{ row } * columns) + column) * 2);
    }

    [[nodiscard]] gfx::FogFrame Frame() const {
        return { .columns = columns, .rows = rows, .tileSize = TILE, .values = values, .revision = 1 };
    }
};

/// Four clouds, held still (no bob) unless a case sets one, so a foot is where the cloud rests.
core::FogLook StillLook() {
    core::FogLook look;
    look.clouds = { SpriteId::Cloud0, SpriteId::Cloud1, SpriteId::Cloud2, SpriteId::Cloud3 };
    look.bobAmplitude = 0.0F;
    return look;
}

/// The world point `column` and `row` tiles from the map's origin.
constexpr core::Vec2 AtTiles(f32 column, f32 row) {
    return { .x = column * TILE, .y = row * TILE };
}

/// What a 16:9 window centred on `centre` shows at `zoom`.
core::Rect WindowAt(core::Vec2 centre, f32 zoom) {
    const core::Vec2 half = core::REFERENCE_SIZE * (HALF / zoom);
    return { .min = centre - half, .max = centre + half };
}

gfx::CloudDrawing Drawn(const Field& field, const core::FogLook& look, core::Rect view, f32 zoom, f64 seconds = 0.0) {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    gfx::CloudDrawing drawing;
    gfx::DrawClouds(field.Frame(), look, *manifest, view, zoom, seconds, drawing);
    return drawing;
}

core::Vec2 Size(const core::Instance& instance) {
    return { .x = std::fabs(core::UnpackHalf(static_cast<u16>(instance.size & 0xFFFFU))),
             .y = std::fabs(core::UnpackHalf(static_cast<u16>(instance.size >> HIGH_HALF))) };
}

/// The cloud sprites stand on their centre, so an instance's centre is its foot.
core::Vec2 Foot(const core::Instance& instance) {
    return instance.current;
}

f32 Opacity(const core::Instance& instance) {
    return static_cast<f32>((instance.colour >> ALPHA_SHIFT) & BYTE) / static_cast<f32>(BYTE);
}

/// The level a cloud was laid out at, from its width: with the look's cloud width of 1.15 cells and the 0.85 to 1.45 draw,
/// a level-0 cloud is 0.49 to 0.83 tiles wide, a level-1 one 0.98 to 1.67 and a level-2 one 1.96 to 3.34.
std::size_t LevelOf(const core::Instance& instance) {
    const f32 tiles = Size(instance).x / TILE;
    if (tiles < 0.9F) {
        return 0;
    }
    return tiles < 1.8F ? 1 : 2;
}

/// Each level's clouds: how many, and their summed opacity.
struct Levels {
    std::array<std::size_t, LEVELS> count{};
    std::array<f32, LEVELS> opacity{};

    explicit Levels(const gfx::CloudDrawing& drawing) {
        for (const core::Instance& instance : drawing.instances) {
            const std::size_t level = LevelOf(instance);
            ++count.at(level);
            opacity.at(level) += Opacity(instance);
        }
    }

    [[nodiscard]] f32 Mean(std::size_t level) const {
        return count.at(level) == 0 ? 0.0F : opacity.at(level) / static_cast<f32>(count.at(level));
    }
};

bool Same(const core::Instance& lhs, const core::Instance& rhs) {
    return std::ranges::equal(std::as_bytes(std::span{ &lhs, 1 }), std::as_bytes(std::span{ &rhs, 1 }));
}

bool SameAll(const gfx::CloudDrawing& lhs, const gfx::CloudDrawing& rhs) {
    return std::ranges::equal(lhs.instances, rhs.instances, Same);
}

/// Whether some tile `instance`'s box overlaps is explored at all or off `field`.
bool ReachesExplored(const Field& field, const core::Instance& instance) {
    const core::Vec2 half = Size(instance) * HALF;
    const core::Vec2 min = instance.current - half;
    const core::Vec2 max = instance.current + half;
    const auto first = [](f32 edge) { return static_cast<i64>(std::floor((edge + SIZE_SLACK) / TILE)); };
    const auto last = [](f32 edge) { return static_cast<i64>(std::floor((edge - SIZE_SLACK) / TILE)); };
    for (i64 row = first(min.y); row <= last(max.y); ++row) {
        for (i64 column = first(min.x); column <= last(max.x); ++column) {
            if (column < 0 || row < 0 || column >= i64{ field.columns } || row >= i64{ field.rows }) {
                return true;
            }
            if (field.At(static_cast<u32>(column), static_cast<u32>(row)) > 0) {
                return true;
            }
        }
    }
    return false;
}

std::shared_ptr<const core::WorldMap> ShippedMap() {
    std::expected<std::shared_ptr<const core::WorldMap>, core::Error> map =
        platform::AssetsDirectory().and_then([](const std::filesystem::path& assets) {
            return data::LoadStrings(assets / "data").and_then([&](const core::Strings& strings) {
                return data::LoadWorldMap(assets / "data", strings);
            });
        });
    REQUIRE(map.has_value());
    return std::move(*map);
}

// A 16 x 16 map with a diamond of radius 2 explored round (8, 8), a lone tile at (3, 12), the top-right corner and the
// left column's middle.
Field Explored() {
    Field field{ SIDE, SIDE };
    for (u32 row = 6; row <= 10; ++row) {
        for (u32 column = 6; column <= 10; ++column) {
            const u32 across = column > 8 ? column - 8 : 8 - column;
            const u32 down = row > 8 ? row - 8 : 8 - row;
            if (across + down <= 2) {
                field.Set(column, row, FULL);
            }
        }
    }
    field.Set(3, 12, FULL);
    field.Set(15, 0, FULL);
    field.Set(0, 7, FULL);
    return field;
}

constexpr core::Vec2 MIDDLE = AtTiles(8.0F, 8.0F);

TEST_CASE("the levels at zoom 1, 0.6, 0.4 and 0.25 are 0, 1, 1 and 2, and 0.71 and 0.35 cross-fade half and half") {
    CHECK(gfx::CloudLevel(2.0F) == 0.0F);
    CHECK(gfx::CloudLevel(1.0F) == 0.0F);
    CHECK(gfx::CloudLevel(0.5F) == 1.0F);
    CHECK(gfx::CloudLevel(0.25F) == 2.0F);
    // A map wider than any of these views, so every cloud is over unexplored land and none is a map edge's split.
    const Field field{ 2 * SIDE, 2 * SIDE };
    const core::FogLook look = StillLook();
    const core::Vec2 centre = AtTiles(16.0F, 16.0F);
    struct Alone {
        f32 zoom;
        std::size_t level;
    };
    for (const Alone alone : { Alone{ .zoom = 1.0F, .level = 0 },
                               Alone{ .zoom = 0.6F, .level = 1 },
                               Alone{ .zoom = 0.4F, .level = 1 },
                               Alone{ .zoom = 0.25F, .level = 2 } }) {
        CAPTURE(alone.zoom);
        const Levels levels{ Drawn(field, look, WindowAt(centre, alone.zoom), alone.zoom) };
        for (std::size_t level = 0; level < LEVELS; ++level) {
            CHECK((levels.count.at(level) > 0) == (level == alone.level));
        }
        CHECK(levels.Mean(alone.level) == doctest::Approx(1.0F));
    }
    const Levels close{ Drawn(field, look, WindowAt(centre, 0.71F), 0.71F) };
    CHECK(close.count.at(2) == 0);
    CHECK(close.Mean(0) == doctest::Approx(0.52F).epsilon(0.01));
    CHECK(close.Mean(1) == doctest::Approx(0.48F).epsilon(0.01));
    const Levels far{ Drawn(field, look, WindowAt(centre, 0.35F), 0.35F) };
    CHECK(far.count.at(0) == 0);
    CHECK(far.Mean(1) == doctest::Approx(0.45F).epsilon(0.01));
    CHECK(far.Mean(2) == doctest::Approx(0.55F).epsilon(0.01));
}

TEST_CASE("the same view gives the same clouds") {
    const Field field = Explored();
    core::FogLook look; // the default bob and drift, so they are part of what has to repeat
    look.clouds = StillLook().clouds;
    const gfx::CloudDrawing first = Drawn(field, look, WindowAt(MIDDLE, 0.71F), 0.71F, 1.5);
    const gfx::CloudDrawing second = Drawn(field, look, WindowAt(MIDDLE, 0.71F), 0.71F, 1.5);
    CHECK_FALSE(first.instances.empty());
    CHECK(SameAll(first, second));
}

TEST_CASE("panning away and back gives the same clouds") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const Field field = Explored();
    const core::FogLook look = StillLook();
    const core::Rect here = WindowAt(MIDDLE, 1.0F);
    const core::Rect there = WindowAt(AtTiles(11.0F, 9.0F), 1.0F);
    gfx::CloudDrawing drawing;
    gfx::DrawClouds(field.Frame(), look, *manifest, here, 1.0F, 0.0, drawing);
    const gfx::CloudDrawing before = drawing;
    gfx::DrawClouds(field.Frame(), look, *manifest, there, 1.0F, 0.0, drawing);
    const gfx::CloudDrawing panned = drawing;
    gfx::DrawClouds(field.Frame(), look, *manifest, here, 1.0F, 0.0, drawing);
    CHECK(SameAll(before, drawing));
    // A cloud both views show is the same cloud in each, so its place comes from the map, not the view.
    const core::Rect both{ .min = { .x = std::max(here.min.x, there.min.x), .y = std::max(here.min.y, there.min.y) },
                           .max = { .x = std::min(here.max.x, there.max.x), .y = std::min(here.max.y, there.max.y) } };
    std::size_t shared = 0;
    std::size_t missing = 0;
    for (const core::Instance& instance : before.instances) {
        if (core::Contains(both, Foot(instance))) {
            ++shared;
            const auto same = [&](const core::Instance& other) { return Same(instance, other); };
            missing += std::ranges::none_of(panned.instances, same) ? 1 : 0;
        }
    }
    CHECK(shared > 0);
    CHECK(missing == 0);
}

TEST_CASE("an all-unexplored map at zoom 0.25 draws whole level-2 clouds only") {
    const Field field{ SIDE, SIDE };
    const gfx::CloudDrawing drawing = Drawn(field, StillLook(), WindowAt(MIDDLE, 0.25F), 0.25F);
    const Levels levels{ drawing };
    CHECK(levels.count.at(0) == 0);
    CHECK(levels.count.at(1) == 0);
    // 8 x 8 cells of 2 x 2 tiles, two or three clouds each.
    CHECK(levels.count.at(2) >= 128);
    CHECK(levels.count.at(2) <= 192);
    CHECK(levels.Mean(2) == doctest::Approx(1.0F));
}

TEST_CASE("a cell touching an explored tile splits into finer clouds") {
    const core::FogLook look = StillLook();
    const core::Rect view = WindowAt(MIDDLE, 0.25F);
    const Levels whole{ Drawn(Field{ SIDE, SIDE }, look, view, 0.25F) };
    Field field{ SIDE, SIDE };
    field.Set(6, 6, FULL);
    const gfx::CloudDrawing drawing = Drawn(field, look, view, 0.25F);
    const Levels split{ drawing };
    // The cell over tiles 6 and 7 of rows 6 and 7 gives way to its three children that touch no explored tile.
    CHECK(split.count.at(0) == 0);
    CHECK(split.count.at(1) >= 6);
    CHECK(split.count.at(1) <= 9);
    CHECK(whole.count.at(2) - split.count.at(2) >= 2);
    CHECK(whole.count.at(2) - split.count.at(2) <= 3);
    CHECK(split.Mean(1) == doctest::Approx(1.0F));
    const f32 low = (6.0F * TILE) - SIZE_SLACK;
    const f32 high = (8.0F * TILE) + SIZE_SLACK;
    std::size_t astray = 0;
    for (const core::Instance& instance : drawing.instances) {
        const core::Vec2 foot = Foot(instance);
        const bool inside = foot.x >= low && foot.x <= high && foot.y >= low && foot.y <= high;
        astray += LevelOf(instance) == 1 && !inside ? 1 : 0;
    }
    CHECK(astray == 0);
}

TEST_CASE("no cloud's box reaches into an explored tile or past the map") {
    const Field field = Explored();
    core::FogLook look = StillLook();
    look.bobAmplitude = core::DEFAULT_BOB_AMPLITUDE;
    std::size_t clouds = 0;
    std::size_t reaching = 0;
    for (const f32 zoom : { 1.0F, 0.71F, 0.5F, 0.35F, 0.25F }) {
        // At rest, then mid-bob.
        for (const f64 seconds : { 0.0, 1.3 }) {
            for (const core::Instance& instance : Drawn(field, look, WindowAt(MIDDLE, zoom), zoom, seconds).instances) {
                ++clouds;
                reaching += ReachesExplored(field, instance) ? 1 : 0;
            }
        }
    }
    CHECK(clouds > 0);
    CHECK(reaching == 0);
}

TEST_CASE("a cloud's box eases in as its neighbour uncovers, never jumping") {
    // Two tiles side by side: the left tile's clouds that reach over into the right one are pulled back as it uncovers.
    const core::FogLook look = StillLook();
    const core::Rect view = WindowAt(AtTiles(1.0F, HALF), 1.0F);
    const auto drawnWith = [&](u8 right) {
        Field field{ 2, 1 };
        field.Set(1, 0, right);
        return Drawn(field, look, view, 1.0F);
    };
    const gfx::CloudDrawing covered = drawnWith(0);
    const gfx::CloudDrawing starting = drawnWith(3); // about 0.01 uncovered
    const gfx::CloudDrawing explored = drawnWith(FULL);
    Field uncovered{ 2, 1 };
    uncovered.Set(1, 0, FULL);
    // Only the left tile's clouds are left once the right one is explored; each keeps its sprite and size throughout.
    const auto find = [](const gfx::CloudDrawing& drawing, const core::Instance& cloud) {
        const auto same = [&](const core::Instance& other) {
            return other.size == cloud.size && other.regionMin == cloud.regionMin;
        };
        const auto found = std::ranges::find_if(drawing.instances, same);
        REQUIRE(found != drawing.instances.end());
        return Foot(*found);
    };
    REQUIRE_FALSE(explored.instances.empty());
    f32 firstStep = 0.0F;
    f32 wholeWay = 0.0F;
    std::size_t reaching = 0;
    for (const core::Instance& cloud : explored.instances) {
        const core::Vec2 before = find(covered, cloud);
        const core::Vec2 barely = find(starting, cloud) - before;
        const core::Vec2 after = Foot(cloud) - before;
        firstStep = std::max(firstStep, std::hypot(barely.x, barely.y));
        wholeWay = std::max(wholeWay, std::hypot(after.x, after.y));
        reaching += ReachesExplored(uncovered, cloud) ? 1 : 0;
    }
    CHECK(wholeWay > 10.0F); // some cloud did reach over and was pulled in
    CHECK(firstStep < 2.0F); // a hundredth of the way, where a jump would be most of that
    CHECK(reaching == 0);
}

TEST_CASE("a tile halfway uncovered draws its clouds at half opacity, drifted half their distance") {
    constexpr u8 HALFWAY = 128;
    const core::FogLook look = StillLook();
    const core::Rect view = WindowAt(AtTiles(HALF, HALF), 1.0F);
    const gfx::CloudDrawing covered = Drawn(Field{ 1, 1 }, look, view, 1.0F);
    const gfx::CloudDrawing uncovering = Drawn(Field{ 1, 1, HALFWAY }, look, view, 1.0F);
    REQUIRE(covered.instances.size() >= 8); // four cells of a single tile, two or three clouds each
    REQUIRE(uncovering.instances.size() == covered.instances.size());
    const f32 drift = look.drift * TILE * (static_cast<f32>(HALFWAY) / static_cast<f32>(FULL));
    f32 worstDrift = 0.0F;
    f32 worstOpacity = 0.0F;
    for (std::size_t cloud = 0; cloud < covered.instances.size(); ++cloud) {
        const core::Vec2 moved = Foot(uncovering.instances.at(cloud)) - Foot(covered.instances.at(cloud));
        worstDrift = std::max(worstDrift, std::fabs(std::hypot(moved.x, moved.y) - drift));
        worstOpacity = std::max(worstOpacity, std::fabs(Opacity(uncovering.instances.at(cloud)) - (127.0F / 255.0F)));
    }
    CHECK(worstDrift < 0.01F);
    CHECK(worstOpacity < 0.001F);
}

TEST_CASE("a fully explored view draws no clouds") {
    gfx::CloudDrawing drawing = Drawn(Field{ SIDE, SIDE }, StillLook(), WindowAt(MIDDLE, 1.0F), 1.0F);
    REQUIRE_FALSE(drawing.instances.empty());
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const Field explored{ SIDE, SIDE, FULL };
    gfx::DrawClouds(explored.Frame(), StillLook(), *manifest, WindowAt(MIDDLE, 1.0F), 1.0F, 0.0, drawing);
    CHECK(drawing.instances.empty());
    CHECK(drawing.batches.empty());
}

TEST_CASE("no fog or no cloud sprites draws nothing") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const Field field{ SIDE, SIDE };
    gfx::CloudDrawing drawing = Drawn(field, StillLook(), WindowAt(MIDDLE, 1.0F), 1.0F);
    REQUIRE_FALSE(drawing.instances.empty());
    gfx::DrawClouds(gfx::FogFrame{}, StillLook(), *manifest, WindowAt(MIDDLE, 1.0F), 1.0F, 0.0, drawing);
    CHECK(drawing.instances.empty());
    CHECK(drawing.batches.empty());
    gfx::DrawClouds(field.Frame(), core::FogLook{}, *manifest, WindowAt(MIDDLE, 1.0F), 1.0F, 0.0, drawing);
    CHECK(drawing.instances.empty());
    CHECK(drawing.batches.empty());
}

TEST_CASE("clouds are drawn back to front by their foot") {
    // Two levels at once and a split round the explored tiles, so clouds of every size interleave.
    const gfx::CloudDrawing drawing = Drawn(Explored(), StillLook(), WindowAt(MIDDLE, 0.71F), 0.71F);
    REQUIRE(drawing.instances.size() > 1);
    std::size_t outOfOrder = 0;
    std::size_t moving = 0;
    for (std::size_t cloud = 1; cloud < drawing.instances.size(); ++cloud) {
        outOfOrder += Foot(drawing.instances.at(cloud)).y < Foot(drawing.instances.at(cloud - 1)).y ? 1 : 0;
    }
    for (const core::Instance& instance : drawing.instances) {
        moving += std::bit_cast<u64>(instance.previous) == std::bit_cast<u64>(instance.current) ? 0 : 1;
    }
    CHECK(outOfOrder == 0);
    CHECK(moving == 0);
    REQUIRE(drawing.batches.size() == 1);
    CHECK(drawing.batches.front().first == 0);
    CHECK(drawing.batches.front().count == drawing.instances.size());
    CHECK(drawing.batches.front().pipeline == core::Pipeline::Normal);
}

TEST_CASE("a 32:9 view at zoom 0.25 over the shipped map, all unexplored, stays within the buffer") {
    const std::shared_ptr<const core::WorldMap> map = ShippedMap();
    REQUIRE(map->TileSize() == TILE);
    const Field field{ map->Columns(), map->Rows() };
    const core::FogLook look = StillLook();
    const core::Vec2 far{ .x = static_cast<f32>(map->Columns()) * TILE, .y = static_cast<f32>(map->Rows()) * TILE };
    std::size_t widest = 0;
    std::size_t most = 0;
    // The map's middle and two corners, where the view overhangs; the widest zoom, the two cross-fades halfway, where two
    // levels draw at once, and zoom 1, the widest view of the finest clouds.
    struct Sample {
        f32 zoom;
        bool widest;
    };
    for (const core::Vec2 centre : { far * HALF, core::Vec2{}, far }) {
        for (const Sample sample : { Sample{ .zoom = 0.25F, .widest = true },
                                     Sample{ .zoom = 0.35F, .widest = false },
                                     Sample{ .zoom = 0.71F, .widest = false },
                                     Sample{ .zoom = 1.0F, .widest = false } }) {
            const f32 high = core::REFERENCE_SIZE.y / sample.zoom;
            const core::Vec2 half{ .x = high * (32.0F / 9.0F) * HALF, .y = high * HALF };
            const core::Rect view{ .min = centre - half, .max = centre + half };
            const std::size_t count = Drawn(field, look, view, sample.zoom).instances.size();
            most = std::max(most, count);
            widest = sample.widest ? std::max(widest, count) : widest;
        }
    }
    CAPTURE(widest);
    CAPTURE(most);
    CHECK(widest > 0);
    CHECK(most <= gfx::MAX_CLOUD_INSTANCES);
}

TEST_CASE("the bob moves a cloud by at most its amplitude") {
    const Field field{ SIDE, SIDE };
    core::FogLook look = StillLook();
    look.bobAmplitude = core::DEFAULT_BOB_AMPLITUDE;
    look.bobPeriod = core::DEFAULT_BOB_PERIOD;
    const core::Rect view = WindowAt(MIDDLE, 1.0F);
    // Half a period apart every cloud is as far above its rest as below it.
    const gfx::CloudDrawing start = Drawn(field, look, view, 1.0F, 0.0);
    const gfx::CloudDrawing opposite = Drawn(field, look, view, 1.0F, static_cast<f64>(core::DEFAULT_BOB_PERIOD) * 0.5);
    REQUIRE_FALSE(start.instances.empty());
    REQUIRE(opposite.instances.size() == start.instances.size());
    const f32 amplitude = look.bobAmplitude * HALF * TILE; // a level-0 cell is half a tile across
    f32 worst = 0.0F;
    f32 sideways = 0.0F;
    // A quarter turn in, a moment later, and after a long run, where the time is large.
    for (const f64 seconds : { 1.0, 2.7, 1'000'000.3 }) {
        const gfx::CloudDrawing drawing = Drawn(field, look, view, 1.0F, seconds);
        REQUIRE(drawing.instances.size() == start.instances.size());
        for (std::size_t cloud = 0; cloud < start.instances.size(); ++cloud) {
            const f32 rest = (Foot(start.instances.at(cloud)).y + Foot(opposite.instances.at(cloud)).y) * HALF;
            worst = std::max(worst, std::fabs(Foot(drawing.instances.at(cloud)).y - rest));
            sideways = std::max(sideways, std::fabs(Foot(drawing.instances.at(cloud)).x - Foot(start.instances.at(cloud)).x));
        }
    }
    CHECK(worst > 0.5F * amplitude);
    CHECK(worst <= amplitude + 0.01F);
    CHECK(sideways == 0.0F);
}

} // namespace
