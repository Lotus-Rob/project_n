#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/manifest.hpp"
#include "core/view_limits.hpp"
#include "core/world_look.hpp"
#include "core/world_map.hpp"
#include "data/world_look.hpp"
#include "gfx/stamp_draw.hpp"
#include "gfx/terrain_draw.hpp"
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
constexpr std::size_t FAR_TILES = 48;

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
    // Tile 0's own stamp stands on the painting, so the painting is all there is, with its quad past the map's corner.
    REQUIRE(drawing.instances.size() == 1);
    const core::Vec2 foot = FootOf(drawing.instances[0]);
    CHECK(foot.x == doctest::Approx(TILE / 2));
    CHECK(foot.y == doctest::Approx(384.0F));
    CHECK(
        drawing.instances[0].size ==
        core::MakeInstance(*manifest, { .sprite = SpriteId::PaintingCity, .previous = foot, .current = foot }).size
    );
}

TEST_CASE("a painting just past the span is laid out, a stamp there is not") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::WorldMap map = test::MeadowMap(MAP_SIDE, MAP_SIDE);
    constexpr core::TileSpan BESIDE{ .firstColumn = 1, .lastColumn = 2, .firstRow = 1, .lastRow = 2 };
    // The city's tile 0 lies one tile off the span's corner, its painting reaching two tiles in.
    const gfx::TerrainDrawing drawing = Laid(map, MeadowLook(0.0F), *manifest, BESIDE);
    CHECK(drawing.instances.size() == 1);
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
        for (const core::Instance& instance : near.instances) {
            const core::Vec2 foot = FootOf(instance);
            for (const core::Instance& other : next.instances) {
                const core::Vec2 where = FootOf(other);
                if ((where.x <=> foot.x) == 0 && (where.y <=> foot.y) == 0) {
                    largest = std::max(
                        largest, std::abs(static_cast<f32>(AlphaOf(instance)) - static_cast<f32>(AlphaOf(other))) / BYTE_RANGE
                    );
                }
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
    CHECK(misty.batches[0].count == FAR_TILES);
    CHECK(misty.batches[1].first == FAR_TILES);
    CHECK(misty.batches[1].count == FAR_TILES);
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
    const core::Vec2 start = map->TileCentre(map->TileIndex(map->Places()[map->Start()].tile));
    const core::Vec2 middle{ .x = static_cast<f32>(map->Columns()) * map->TileSize() * 0.5F,
                             .y = static_cast<f32>(map->Rows()) * map->TileSize() * 0.5F };
    std::size_t most = 0;
    for (const core::Vec2 centre : { start, middle }) {
        const core::Rect view{ .min = { .x = centre.x - half.x, .y = centre.y - half.y },
                               .max = { .x = centre.x + half.x, .y = centre.y + half.y } };
        gfx::TerrainDrawing drawing;
        gfx::DrawTerrain(*map, *look, *manifest, view, drawing);
        for (const f32 zoom : { 0.25F, 1.0F }) {
            gfx::TerrainDrawing stamped = drawing;
            gfx::DrawStamps(*map, *look, *manifest, map->TilesIn(view), zoom, stamped);
            most = std::max(most, stamped.instances.size());
        }
    }
    CHECK(most > 0);
    CHECK(most < gfx::MAX_TERRAIN_INSTANCES);
}

} // namespace
