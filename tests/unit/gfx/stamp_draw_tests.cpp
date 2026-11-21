#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/manifest.hpp"
#include "core/view_limits.hpp"
#include "core/world_look.hpp"
#include "core/world_map.hpp"
#include "data/world_look.hpp"
#include "data/world_map.hpp"
#include "gfx/stamp_draw.hpp"
#include "gfx/terrain_draw.hpp"
#include "gfx/tile_hash.hpp"
#include "platform/paths.hpp"
#include "support/manifest.hpp"
#include "support/meadow.hpp"
#include "support/shipped_world.hpp"

#include <doctest/doctest.h>

namespace {

using core::atlas::SpriteId;

constexpr f32 TILE = 256.0F;
constexpr u32 ALPHA_SHIFT = 24;
constexpr f32 BYTE_RANGE = 255.0F;
constexpr u32 MAP_SIDE = 24;
// A block of tiles far from the city on tile 0, so no painting reaches it.
constexpr core::TileSpan FAR_SPAN{ .firstColumn = 8, .lastColumn = 15, .firstRow = 8, .lastRow = 13 };
// The tiles laid out for FAR_SPAN: the span widened by the stamps' reach, 1 tile for the meadow look (stamps under a tile
// wide), 2 with its mist puffs beside them.
constexpr std::size_t FAR_TILES = 80;
constexpr std::size_t FAR_MIST_TILES = 120;

/// A meadow's look: stamps of `density` and thinning below `thinBelow`, mist with the chance `mistChance` among them.
core::WorldLook MeadowLook(f32 density, f32 thinBelow = 0.0F, bool mist = false, f32 mistChance = 1.0F) {
    core::WorldLook look;
    look.kinds.resize(2);
    look.kinds[0].stamps = { .sprites = { SpriteId::StampMeadow0, SpriteId::StampMeadow1, SpriteId::StampMeadow2 },
                             .density = density,
                             .size = { { 0.6F, 0.9F } },
                             .jitter = 0.2F,
                             .thinBelow = thinBelow,
                             .mist = mist };
    look.mist = { .sprites = { SpriteId::Mist0, SpriteId::Mist1 },
                  .colour = { 246, 244, 238 },
                  .strength = 0.9F,
                  .chance = mistChance,
                  .width = { { 1.0F, 1.4F } } };
    look.places.city = SpriteId::PaintingCity;
    return look;
}

core::Vec2 FootOf(const core::Instance& instance) {
    return core::SightPoint(instance, 1.0);
}

bool Same(const core::Instance& lhs, const core::Instance& rhs) {
    return std::ranges::equal(std::as_bytes(std::span{ &lhs, 1 }), std::as_bytes(std::span{ &rhs, 1 }));
}

bool SameAll(const gfx::TerrainDrawing& lhs, const gfx::TerrainDrawing& rhs) {
    return lhs.instances.size() == rhs.instances.size() && lhs.batches.size() == rhs.batches.size() &&
           std::ranges::equal(lhs.instances, rhs.instances, Same);
}

gfx::TerrainDrawing Laid(
    const core::WorldMap& map,
    const core::WorldLook& look,
    const core::Manifest& manifest,
    core::TileSpan span,
    f32 zoom = 1.0F
) {
    gfx::TerrainDrawing drawing;
    gfx::DrawStamps(map, look, manifest, span, zoom, drawing);
    return drawing;
}

bool Contains(const gfx::TerrainDrawing& drawing, const core::Instance& wanted) {
    return std::ranges::any_of(drawing.instances, [&](const core::Instance& each) { return Same(each, wanted); });
}

bool HasFoot(const gfx::TerrainDrawing& drawing, core::Vec2 foot) {
    return std::ranges::any_of(drawing.instances, [&](const core::Instance& each) {
        const core::Vec2 where = FootOf(each);
        return (where.x <=> foot.x) == 0 && (where.y <=> foot.y) == 0;
    });
}

u32 AlphaOf(const core::Instance& instance) {
    return instance.colour >> ALPHA_SHIFT;
}

/// The width the instance is drawn at, in world units.
f32 UnpackedWidth(const core::Instance& instance) {
    return std::abs(core::UnpackHalf(static_cast<u16>(instance.size & 0xFFFFU)));
}

/// The alpha of the instance of `drawing` with its foot at `foot`, or 0 when it has none there.
u32 AlphaAt(const gfx::TerrainDrawing& drawing, core::Vec2 foot) {
    for (const core::Instance& instance : drawing.instances) {
        const core::Vec2 where = FootOf(instance);
        if ((where.x <=> foot.x) == 0 && (where.y <=> foot.y) == 0) {
            return AlphaOf(instance);
        }
    }
    return 0;
}

TEST_CASE("the same span gives the same stamps") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = test::MeadowMap(MAP_SIDE, MAP_SIDE);
    const core::WorldLook look = MeadowLook(0.5F, 0.0F, true, 0.5F);
    const gfx::TerrainDrawing first = Laid(map, look, *manifest, FAR_SPAN);
    const gfx::TerrainDrawing again = Laid(map, look, *manifest, FAR_SPAN);
    REQUIRE(!first.instances.empty());
    CHECK(SameAll(first, again));
}

TEST_CASE("a stamp is the same seen from two overlapping spans") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = test::MeadowMap(MAP_SIDE, MAP_SIDE);
    const core::WorldLook look = MeadowLook(0.6F, 0.0F, true, 0.5F);
    constexpr core::TileSpan LEFT{ .firstColumn = 6, .lastColumn = 13, .firstRow = 8, .lastRow = 13 };
    constexpr core::TileSpan RIGHT{ .firstColumn = 10, .lastColumn = 17, .firstRow = 8, .lastRow = 13 };
    const gfx::TerrainDrawing left = Laid(map, look, *manifest, LEFT);
    const gfx::TerrainDrawing right = Laid(map, look, *manifest, RIGHT);
    // A stamp's foot lies within its tile's column, so those of columns 10 to 13 are seen by both.
    std::size_t shared = 0;
    std::size_t missing = 0;
    for (const core::Instance& instance : left.instances) {
        const f32 column = std::floor(FootOf(instance).x / TILE);
        if (column >= 10.0F && column <= 13.0F) {
            ++shared;
            missing += Contains(right, instance) ? 0 : 1;
        }
    }
    CHECK(shared > 0);
    CHECK(missing == 0);
}

TEST_CASE("density 0 draws no stamp and density 1 one per tile") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = test::MeadowMap(MAP_SIDE, MAP_SIDE);
    const gfx::TerrainDrawing none = Laid(map, MeadowLook(0.0F), *manifest, FAR_SPAN);
    CHECK(none.instances.empty());
    CHECK(none.batches.empty());
    const gfx::TerrainDrawing all = Laid(map, MeadowLook(1.0F), *manifest, FAR_SPAN);
    CHECK(all.instances.size() == FAR_TILES);
    REQUIRE(all.batches.size() == 1);
    CHECK(all.batches[0].first == 0);
    CHECK(all.batches[0].count == FAR_TILES);
    CHECK(all.batches[0].pipeline == core::Pipeline::Normal);
    CHECK(std::ranges::all_of(all.instances, [](const core::Instance& instance) {
        return instance.flags == core::INSTANCE_FOGGED_LAND && (instance.previous.x <=> instance.current.x) == 0 &&
               (instance.previous.y <=> instance.current.y) == 0;
    }));
}

TEST_CASE("stamps are drawn back to front by foot") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = test::MeadowMap(MAP_SIDE, MAP_SIDE);
    const gfx::TerrainDrawing drawing = Laid(map, MeadowLook(1.0F), *manifest, FAR_SPAN);
    REQUIRE(drawing.instances.size() == FAR_TILES);
    std::size_t outOfOrder = 0;
    for (std::size_t i = 1; i < drawing.instances.size(); ++i) {
        const core::Vec2 before = FootOf(drawing.instances[i - 1]);
        const core::Vec2 after = FootOf(drawing.instances[i]);
        outOfOrder += ((before.y <=> after.y) < 0 || ((before.y <=> after.y) == 0 && (before.x <=> after.x) <= 0)) ? 0 : 1;
    }
    CHECK(outOfOrder == 0);
}

TEST_CASE("stamps under a city's painting make way") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = test::MeadowMap(MAP_SIDE, MAP_SIDE);
    const core::WorldLook look = MeadowLook(1.0F);
    constexpr core::TileSpan AROUND_CITY{ .firstColumn = 0, .lastColumn = 4, .firstRow = 0, .lastRow = 4 };
    const gfx::TerrainDrawing drawing = Laid(map, look, *manifest, AROUND_CITY);
    // The painting is 512 square, centred on tile (0, 0)'s centre: it covers x and y from -128 to 384.
    const core::SpriteInfo& info = manifest->Sprite(SpriteId::PaintingCity);
    const core::Vec2 foot{ .x = (TILE / 2) - (static_cast<f32>(info.width) / 2) + static_cast<f32>(info.footX),
                           .y = (TILE / 2) - (static_cast<f32>(info.height) / 2) + static_cast<f32>(info.footY) };
    const core::Instance painting = core::MakeInstance(
        *manifest, { .sprite = SpriteId::PaintingCity, .previous = foot, .current = foot, .flags = core::INSTANCE_FOGGED_LAND }
    );
    CHECK(Contains(drawing, painting));
    std::size_t under = 0;
    for (const core::Instance& instance : drawing.instances) {
        const core::Vec2 where = FootOf(instance);
        under +=
            (where.x >= -128.0F && where.x <= 384.0F && where.y >= -128.0F && where.y <= 384.0F && !Same(instance, painting))
                ? 1
                : 0;
    }
    CHECK(under == 0);
    // The tiles the painting leaves bare still wear stamps: 25 tiles, at most the 4 the painting reaches go without.
    CHECK(drawing.instances.size() > 20);
}

TEST_CASE("a place's own painting wins over its kind's") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = test::MeadowMap(MAP_SIDE, MAP_SIDE);
    core::WorldLook look = MeadowLook(0.0F);
    look.places.byKey = { { "city", SpriteId::PaintingCapital } };
    constexpr core::TileSpan CITY_TILE{ .firstColumn = 0, .lastColumn = 0, .firstRow = 0, .lastRow = 0 };
    const gfx::TerrainDrawing drawing = Laid(map, look, *manifest, CITY_TILE);
    const core::SpriteInfo& info = manifest->Sprite(SpriteId::PaintingCapital);
    const core::Vec2 foot{ .x = (TILE / 2) - (static_cast<f32>(info.width) / 2) + static_cast<f32>(info.footX),
                           .y = (TILE / 2) - (static_cast<f32>(info.height) / 2) + static_cast<f32>(info.footY) };
    REQUIRE(drawing.instances.size() == 1);
    CHECK(Same(
        drawing.instances[0],
        core::MakeInstance(
            *manifest,
            { .sprite = SpriteId::PaintingCapital, .previous = foot, .current = foot, .flags = core::INSTANCE_FOGGED_LAND }
        )
    ));
}

TEST_CASE("a painting on the corner tile is laid out whole and its stamps skipped") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = test::MeadowMap(MAP_SIDE, MAP_SIDE);
    constexpr core::TileSpan CORNER{ .firstColumn = 0, .lastColumn = 0, .firstRow = 0, .lastRow = 0 };
    const gfx::TerrainDrawing drawing = Laid(map, MeadowLook(1.0F), *manifest, CORNER);
    // The painting's quad reaches past the map's corner, and no stamp stands on it: of the four tiles laid out, the corner's
    // and one beside it have their feet on it, leaving two stamps and the painting.
    std::size_t paintings = 0;
    std::size_t under = 0;
    for (const core::Instance& instance : drawing.instances) {
        const core::Vec2 foot = FootOf(instance);
        if (UnpackedWidth(instance) > TILE) {
            ++paintings;
            CHECK(foot.x == doctest::Approx(TILE / 2));
            CHECK(foot.y == doctest::Approx(384.0F));
        }
        else if (foot.x <= 384.0F && foot.y <= 384.0F) {
            ++under;
        }
    }
    CHECK(paintings == 1);
    CHECK(under == 0);
    CHECK(drawing.instances.size() == 3);
}

TEST_CASE("a painting just past the span is laid out") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = test::MeadowMap(MAP_SIDE, MAP_SIDE);
    constexpr core::TileSpan BESIDE{ .firstColumn = 1, .lastColumn = 2, .firstRow = 1, .lastRow = 2 };
    // The city's tile 0 lies one tile off the span's corner, its painting reaching two tiles in.
    const gfx::TerrainDrawing drawing = Laid(map, MeadowLook(0.0F), *manifest, BESIDE);
    CHECK(drawing.instances.size() == 1);
}

TEST_CASE("a stamp just past the span is laid out") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = test::MeadowMap(MAP_SIDE, MAP_SIDE);
    core::WorldLook look = MeadowLook(1.0F);
    look.kinds[0].stamps.size = { { 3.5F, 4.0F } };
    constexpr core::TileSpan ONE_ROW{ .firstColumn = 8, .lastColumn = 15, .firstRow = 10, .lastRow = 10 };
    const gfx::TerrainDrawing drawing = Laid(map, look, *manifest, ONE_ROW);
    // A stamp 4 tiles wide stands up about 1.5 tiles from its foot: one on the row below, or the column beside, shows in view.
    std::size_t below = 0;
    std::size_t beside = 0;
    for (const core::Instance& instance : drawing.instances) {
        const core::Vec2 foot = FootOf(instance);
        below += std::floor(foot.y / TILE) == 11.0F ? 1 : 0;
        beside += std::floor(foot.x / TILE) == 16.0F ? 1 : 0;
    }
    CHECK(below > 0);
    CHECK(beside > 0);
}

TEST_CASE("stamps are never laid out past the map's edge") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = test::MeadowMap(MAP_SIDE, MAP_SIDE);
    core::WorldLook look = MeadowLook(1.0F, 0.0F, true, 1.0F);
    look.kinds[0].stamps.size = { { 3.5F, 4.0F } };
    constexpr core::TileSpan CORNER{
        .firstColumn = MAP_SIDE - 2, .lastColumn = MAP_SIDE - 1, .firstRow = MAP_SIDE - 2, .lastRow = MAP_SIDE - 1
    };
    const gfx::TerrainDrawing drawing = Laid(map, look, *manifest, CORNER);
    // One stamp and one puff a tile at most, and the map holds MAP_SIDE squared of them.
    CHECK(drawing.instances.size() <= std::size_t{ 2 } * MAP_SIDE * MAP_SIDE);
    CHECK(drawing.instances.size() > 8);
}

TEST_CASE("a danger's painting is its sort's, else the city's") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    constexpr std::string_view MAP = R"yaml(road_cost: 4
ford_cost: 30
tile_size: 256
kinds:
  - { symbol: ".", name: meadow, cost: 10 }
grid: |
  ......
  ......
  ......
profiles:
  - { key: on_foot, ticks_per_cost: 16 }
places:
  - { key: capital, kind: city, tile: [0, 0], name: place_capital, people: 100, start: true }
  - { key: wolf_pass, kind: danger, sort: mountain_pass, tile: [5, 2], name: place_wolf_pass }
  - { key: old_cave, kind: danger, sort: cave, tile: [3, 2], name: place_old_cave }
)yaml";
    core::Strings strings;
    strings.Add("place_capital", { { "Capital", "都城" } });
    strings.Add("place_wolf_pass", { { "Wolf Pass", "狼关" } });
    strings.Add("place_old_cave", { { "Old Cave", "古洞" } });
    std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(MAP, strings);
    REQUIRE(map.has_value());
    core::WorldLook look = MeadowLook(0.0F);
    look.places.dangers = { { "mountain_pass", SpriteId::PaintingMountainPass } };
    constexpr core::TileSpan DANGERS{ .firstColumn = 3, .lastColumn = 5, .firstRow = 2, .lastRow = 2 };
    const gfx::TerrainDrawing drawing = Laid(*map, look, *manifest, DANGERS);
    // The pass has its sort's painting, the cave (a sort the look lacks) the city's; each centred on its tile.
    const auto painted = [&](SpriteId sprite, u32 column) {
        const core::SpriteInfo& info = manifest->Sprite(sprite);
        const core::Vec2 foot{ .x = ((static_cast<f32>(column) + 0.5F) * TILE) - (static_cast<f32>(info.width) / 2) +
                                    static_cast<f32>(info.footX),
                               .y = (2.5F * TILE) - (static_cast<f32>(info.height) / 2) + static_cast<f32>(info.footY) };
        return core::MakeInstance(
            *manifest, { .sprite = sprite, .previous = foot, .current = foot, .flags = core::INSTANCE_FOGGED_LAND }
        );
    };
    CHECK(Contains(drawing, painted(SpriteId::PaintingMountainPass, 5)));
    CHECK(Contains(drawing, painted(SpriteId::PaintingCity, 3)));
    CHECK(drawing.instances.size() == 2);
}

TEST_CASE("a stamp's width and mirroring and its puff's centre, width and colour follow the tile's draws") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = test::MeadowMap(MAP_SIDE, MAP_SIDE);
    core::WorldLook look = MeadowLook(1.0F, 0.0F, true, 1.0F);
    look.kinds[0].stamps.jitter = 0.0F;
    const core::TileSpan span{ .firstColumn = 10, .lastColumn = 15, .firstRow = 10, .lastRow = 10 };
    const gfx::TerrainDrawing drawing = Laid(map, look, *manifest, span);
    REQUIRE(drawing.batches.size() == 2);
    const std::size_t puffsFrom = drawing.batches[1].first;
    // The instance among `count` from `from` whose foot (or centre, for a puff) is at `foot`, a half-precision height's
    // rounding allowed.
    const auto find = [&](std::size_t from, std::size_t count, core::Vec2 foot, bool byCentre) -> const core::Instance* {
        for (std::size_t index = from; index < from + count; ++index) {
            const core::Vec2 found = byCentre ? drawing.instances[index].current : FootOf(drawing.instances[index]);
            if (std::abs(found.x - foot.x) < 0.5F && std::abs(found.y - foot.y) < 0.5F) {
                return &drawing.instances[index];
            }
        }
        return nullptr;
    };
    std::size_t checked = 0;
    std::size_t wrong = 0;
    for (u32 column = 10; column <= 15; ++column) {
        // With no jitter a stamp stands at the middle of its tile, three quarters down.
        const core::Vec2 foot{ .x = (static_cast<f32>(column) + 0.5F) * TILE, .y = 10.75F * TILE };
        const f32 stampWidth = std::lerp(0.6F, 0.9F, gfx::TileUnit(column, 10, 6)) * TILE;
        const bool mirrored = gfx::TileUnit(column, 10, 5) < 0.5F;
        const f32 puffWidth = std::lerp(1.0F, 1.4F, gfx::TileUnit(column, 10, 11)) * stampWidth;
        // A puff's sprite is centred on its foot, so its centre is the stamp's foot.
        const core::Instance* stamp = find(0, puffsFrom, foot, false);
        const core::Instance* puff = find(puffsFrom, drawing.instances.size() - puffsFrom, foot, true);
        if (stamp == nullptr || puff == nullptr) {
            continue;
        }
        checked += 2;
        wrong += std::abs(UnpackedWidth(*stamp) - stampWidth) > 0.01F * stampWidth ? 1 : 0;
        wrong += (core::UnpackHalf(static_cast<u16>(stamp->size & 0xFFFFU)) < 0.0F) != mirrored ? 1 : 0;
        wrong += std::abs(UnpackedWidth(*puff) - puffWidth) > 0.01F * puffWidth ? 1 : 0;
        // 0.9 strength at full opacity over 246 red.
        wrong += std::abs(static_cast<f32>(AlphaOf(*puff)) - (0.9F * BYTE_RANGE)) > 1.0F ? 1 : 0;
        wrong += std::abs(static_cast<f32>(puff->colour & 0xFFU) - (246.0F * 0.9F)) > 1.0F ? 1 : 0;
    }
    // Both a stamp and a puff of each of the six tiles were found and held to their draws.
    CHECK(checked == 12);
    CHECK(wrong == 0);
}

TEST_CASE("thinning drops the lowest-ranked stamps first, the same ones each time") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = test::MeadowMap(MAP_SIDE, MAP_SIDE);
    const core::WorldLook look = MeadowLook(1.0F, 0.5F);
    const gfx::TerrainDrawing gone = Laid(map, look, *manifest, FAR_SPAN, 0.25F);
    const gfx::TerrainDrawing few = Laid(map, look, *manifest, FAR_SPAN, 0.32F);
    const gfx::TerrainDrawing more = Laid(map, look, *manifest, FAR_SPAN, 0.42F);
    const gfx::TerrainDrawing all = Laid(map, look, *manifest, FAR_SPAN, 0.5F);
    const gfx::TerrainDrawing fewAgain = Laid(map, look, *manifest, FAR_SPAN, 0.32F);
    CHECK(gone.instances.empty());
    CHECK(all.instances.size() == FAR_TILES);
    CHECK(few.instances.size() > 0);
    CHECK(few.instances.size() < more.instances.size());
    CHECK(more.instances.size() < FAR_TILES);
    CHECK(SameAll(few, fewAgain));
    // Every stamp still drawn at the lower zoom is drawn at the higher one, at the same place.
    std::size_t dropped = 0;
    for (const core::Instance& instance : few.instances) {
        dropped += HasFoot(more, FootOf(instance)) ? 0 : 1;
    }
    CHECK(dropped == 0);
}

TEST_CASE("a stamp's opacity moves by at most 0.1 between zooms 1% apart") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = test::MeadowMap(MAP_SIDE, MAP_SIDE);
    const core::WorldLook look = MeadowLook(1.0F, 0.5F);
    constexpr std::array ZOOMS{ 0.26F, 0.3F, 0.34F, 0.38F, 0.42F, 0.46F, 0.49F };
    f32 largest = 0.0F;
    for (const f32 zoom : ZOOMS) {
        const gfx::TerrainDrawing near = Laid(map, look, *manifest, FAR_SPAN, zoom);
        const gfx::TerrainDrawing next = Laid(map, look, *manifest, FAR_SPAN, zoom * 1.01F);
        // A stamp one zoom lacks counts as opacity 0 there, so one that appears or vanishes outright is a step too.
        for (const gfx::TerrainDrawing* drawing : { &near, &next }) {
            for (const core::Instance& instance : drawing->instances) {
                const core::Vec2 foot = FootOf(instance);
                largest = std::max(
                    largest,
                    std::abs(static_cast<f32>(AlphaAt(near, foot)) - static_cast<f32>(AlphaAt(next, foot))) / BYTE_RANGE
                );
            }
        }
    }
    CHECK(largest <= 0.1F);
}

TEST_CASE("a kind without mist gets no puffs") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = test::MeadowMap(MAP_SIDE, MAP_SIDE);
    const gfx::TerrainDrawing bare = Laid(map, MeadowLook(1.0F, 0.0F, false), *manifest, FAR_SPAN);
    CHECK(bare.batches.size() == 1);
    CHECK(bare.instances.size() == FAR_TILES);
    // With mist and a sure chance, each stamp has a puff centred on its foot, in a batch after the stamps.
    const gfx::TerrainDrawing misty = Laid(map, MeadowLook(1.0F, 0.0F, true, 1.0F), *manifest, FAR_SPAN);
    REQUIRE(misty.batches.size() == 2);
    CHECK(misty.batches[0].count == FAR_MIST_TILES);
    CHECK(misty.batches[1].first == FAR_MIST_TILES);
    CHECK(misty.batches[1].count == FAR_MIST_TILES);
    CHECK(misty.batches[1].pipeline == core::Pipeline::Normal);
    // No chance, no puff.
    const gfx::TerrainDrawing calm = Laid(map, MeadowLook(1.0F, 0.0F, true, 0.0F), *manifest, FAR_SPAN);
    CHECK(calm.batches.size() == 1);
}

TEST_CASE("the widest 32:9 view of the shipped map fits the terrain buffer") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    std::expected<core::WorldLook, core::Error> look =
        platform::AssetsDirectory().and_then([&](const std::filesystem::path& assets) {
            return data::LoadWorldLook(assets / "data", *manifest, *map);
        });
    REQUIRE(look.has_value());
    const core::Vec2 half{ .x = core::WIDEST_HALF_VIEW.x, .y = core::WIDEST_HALF_VIEW.x * 9.0F / 32.0F };
    const f32 side = map->TileSize();
    // The window shows 60 by 17 tiles; these are the tile centres where the shipped map is busiest, each for its reason.
    constexpr std::array<core::TilePoint, 4> CENTRES{ {
        // The densest window by expected stamps (about 430, forest-heavy), the south-west corner.
        { .column = 30, .row = 76 },
        // The densest of peaks, rock and snow (0.6 and 0.35 a tile), the stamps that stand tallest.
        { .column = 33, .row = 54 },
        // The most places in one window (six), so the most paintings over the stamps.
        { .column = 57, .row = 21 },
        // The start, beside the capital's 768 square painting.
        { .column = 75, .row = 42 },
    } };
    std::size_t most = 0;
    for (const core::TilePoint& tile : CENTRES) {
        const core::Vec2 centre{ .x = (static_cast<f32>(tile.column) + 0.5F) * side,
                                 .y = (static_cast<f32>(tile.row) + 0.5F) * side };
        const core::Rect view{ .min = { .x = centre.x - half.x, .y = centre.y - half.y },
                               .max = { .x = centre.x + half.x, .y = centre.y + half.y } };
        gfx::TerrainDrawing terrain;
        gfx::DrawTerrain(*map, *look, *manifest, view, terrain);
        for (const f32 zoom : { 0.25F, 1.0F }) {
            gfx::TerrainDrawing drawing = terrain;
            gfx::DrawStamps(*map, *look, *manifest, map->TilesIn(view), zoom, drawing);
            most = std::max(most, drawing.instances.size());
        }
    }
    CHECK(most > 0);
    CHECK(most < gfx::MAX_TERRAIN_INSTANCES);
}

} // namespace
