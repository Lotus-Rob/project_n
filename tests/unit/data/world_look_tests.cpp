#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/manifest.hpp"
#include "core/world_look.hpp"
#include "data/strings.hpp"
#include "data/world_look.hpp"
#include "data/world_map.hpp"
#include "platform/paths.hpp"
#include "support/manifest.hpp"

#include <doctest/doctest.h>

namespace {

// A map of deep water and meadow with one danger, a mountain pass.
constexpr std::string_view SMALL_MAP = R"yaml(road_cost: 4
ford_cost: 30
tile_size: 256
kinds:
  - { symbol: "~", name: deep_water, cost: impassable }
  - { symbol: ".", name: meadow, cost: 10 }
grid: |
  ~~~~~
  ..~..
  .....
roads: [ [[0, 2], [4, 2]] ]
rivers: [ [[3, 1], [3, 2]] ]
fords: [ [3, 1] ]
profiles:
  - { key: on_foot, ticks_per_cost: 16 }
places:
  - { key: capital, kind: city, tile: [0, 2], name: place_capital, people: 100, start: true }
  - { key: wolf_pass, kind: danger, sort: mountain_pass, tile: [4, 1], name: place_wolf_pass }
)yaml";

constexpr std::string_view SMALL_LOOK = R"yaml(kinds:
  deep_water: [terrain_water_0]
  meadow: [terrain_meadow_0, terrain_meadow_1]
roads:
  [road_0, road_1, road_2, road_3, road_4, road_5, road_6, road_7, road_8, road_9, road_10, road_11, road_12, road_13, road_14, road_15]
rivers:
  [river_0, river_1, river_2, river_3, river_4, river_5, river_6, river_7, river_8, river_9, river_10, river_11, river_12, river_13, river_14, river_15]
bridge: { across: bridge_h, down: bridge_v }
ford: ford
markers:
  city: marker_city
  mountain_pass: marker_mountain_pass
)yaml";

constexpr std::size_t MASKS = 16;

core::WorldMap SmallMap() {
    core::Strings strings;
    strings.Add("place_capital", { { "Capital", "都城" } });
    strings.Add("place_wolf_pass", { { "Wolf Pass", "狼关" } });
    std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(SMALL_MAP, strings);
    REQUIRE(map.has_value());
    return std::move(*map);
}

// SMALL_LOOK with `from` replaced by `replacement`.
std::string With(std::string_view from, std::string_view replacement) {
    std::string text{ SMALL_LOOK };
    const std::size_t where = text.find(from);
    REQUIRE(where != std::string::npos);
    return text.replace(where, from.size(), replacement);
}

// The line of `text` (1-based) that `needle` first appears on.
u32 LineOf(std::string_view text, std::string_view needle) {
    const std::size_t where = text.find(needle);
    REQUIRE(where != std::string_view::npos);
    return 1 + static_cast<u32>(std::ranges::count(text.substr(0, where), '\n'));
}

std::expected<core::WorldLook, core::Error> Parse(std::string_view text, std::vector<std::string>* warnings = nullptr) {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    return data::ParseWorldLook(text, *manifest, SmallMap(), warnings);
}

TEST_CASE("a small look parses against a small map") {
    const std::expected<core::WorldLook, core::Error> look = Parse(SMALL_LOOK);
    REQUIRE(look.has_value());
    REQUIRE(look->kinds.size() == 2);
    CHECK(look->kinds[0] == std::vector{ core::atlas::SpriteId::TerrainWater0 });
    CHECK(look->kinds[1] == std::vector{ core::atlas::SpriteId::TerrainMeadow0, core::atlas::SpriteId::TerrainMeadow1 });
    CHECK(look->roads.front() == core::atlas::SpriteId::Road0);
    CHECK(look->rivers.front() == core::atlas::SpriteId::River0);
    CHECK(look->roads.size() == MASKS);
    CHECK(look->bridgeAcross == core::atlas::SpriteId::BridgeH);
    CHECK(look->bridgeDown == core::atlas::SpriteId::BridgeV);
    CHECK(look->ford == core::atlas::SpriteId::Ford);
    CHECK(look->cityMarker == core::atlas::SpriteId::MarkerCity);
    REQUIRE(look->dangerMarkers.size() == 1);
    CHECK(look->dangerMarkers[0].first == "mountain_pass");
    CHECK(look->dangerMarkers[0].second == core::atlas::SpriteId::MarkerMountainPass);
}

TEST_CASE("the look reader refuses what the map can't be drawn by, naming the line") {
    struct Broken {
        std::string_view why;
        std::string text;
        std::string says;
    };
    const std::string noMeadow = With("  meadow: [terrain_meadow_0, terrain_meadow_1]\n", "");
    const std::string noPass = With("  mountain_pass: marker_mountain_pass\n", "");
    const std::string badSprite = With("terrain_water_0", "terrain_lava_0");
    std::string fifteenRoads{ SMALL_LOOK };
    fifteenRoads.replace(fifteenRoads.find(", road_15"), std::string_view{ ", road_15" }.size(), "");
    std::string fifteenRivers{ SMALL_LOOK };
    fifteenRivers.replace(fifteenRivers.find(", river_15"), std::string_view{ ", river_15" }.size(), "");
    const std::vector<Broken> cases{
        { .why = "a kind of the map with no look",
          .text = noMeadow,
          .says = std::format(
              "world_look line {}: the map's kind meadow has no look in kinds", LineOf(SMALL_LOOK, "deep_water:")
          ) },
        { .why = "a kind with no variant",
          .text = With("[terrain_water_0]", "[]"),
          .says = std::format("world_look line {}: kind deep_water has no sprite", LineOf(SMALL_LOOK, "deep_water:")) },
        { .why = "a danger sort the map uses with no marker",
          .text = noPass,
          .says = std::format(
              "world_look line {}: the map's danger sort mountain_pass has no marker in markers", LineOf(SMALL_LOOK, "city:")
          ) },
        { .why = "a sprite the manifest lacks",
          .text = badSprite,
          .says = std::format(
              "world_look line {}: kind deep_water names the sprite \"terrain_lava_0\", which isn't in the atlas",
              LineOf(SMALL_LOOK, "deep_water:")
          ) },
        { .why = "an unused kind naming a sprite the manifest lacks",
          .text = With("  meadow:", "  snow: [terrain_lava_0]\n  meadow:"),
          .says = std::format(
              "world_look line {}: kind snow names the sprite \"terrain_lava_0\", which isn't in the atlas",
              LineOf(With("  meadow:", "  snow: [terrain_lava_0]\n  meadow:"), "snow:")
          ) },
        { .why = "an unused marker naming a sprite the manifest lacks",
          .text = With("  mountain_pass:", "  cave: marker_nowhere\n  mountain_pass:"),
          .says = std::format(
              "world_look line {}: marker cave names the sprite \"marker_nowhere\", which isn't in the atlas",
              LineOf(With("  mountain_pass:", "  cave: marker_nowhere\n  mountain_pass:"), "cave:")
          ) },
        { .why = "a road list not 16 long",
          .text = fifteenRoads,
          .says = std::format("world_look line {}: roads has 15 sprites, not 16", LineOf(SMALL_LOOK, "[road_0")) },
        { .why = "a river list not 16 long",
          .text = fifteenRivers,
          .says = std::format("world_look line {}: rivers has 15 sprites, not 16", LineOf(SMALL_LOOK, "[river_0")) },
        { .why = "a missing bridge",
          .text = With("bridge: { across: bridge_h, down: bridge_v }\n", ""),
          .says = "world_look line 1: the look has no bridge" },
        { .why = "a sprite in the atlas the world scene doesn't load",
          .text = With("ford: ford", "ford: cell_plain"),
          .says = std::format(
              "world_look line {}: ford names the sprite \"cell_plain\", which the world scene doesn't load",
              LineOf(SMALL_LOOK, "ford: ford")
          ) },
        { .why = "an unused kind naming a sprite the world scene doesn't load",
          .text = With("  meadow:", "  snow: [cell_snow]\n  meadow:"),
          .says = std::format(
              "world_look line {}: kind snow names the sprite \"cell_snow\", which the world scene doesn't load",
              LineOf(With("  meadow:", "  snow: [cell_snow]\n  meadow:"), "snow:")
          ) },
        { .why = "a marker with an unknown sprite",
          .text = With("marker_city", "marker_nowhere"),
          .says = std::format(
              "world_look line {}: marker city names the sprite \"marker_nowhere\", which isn't in the atlas",
              LineOf(SMALL_LOOK, "city:")
          ) },
    };
    for (const Broken& broken : cases) {
        CAPTURE(broken.why);
        const std::expected<core::WorldLook, core::Error> look = Parse(broken.text);
        REQUIRE_FALSE(look.has_value());
        CHECK(look.error().message == broken.says);
    }
}

TEST_CASE("a kind or marker the map doesn't use is accepted silently") {
    std::vector<std::string> warnings;
    const std::expected<core::WorldLook, core::Error> look = Parse(
        With("  mountain_pass: marker_mountain_pass\n", "  mountain_pass: marker_mountain_pass\n  cave: marker_cave\n") +
            "kinds_unused: 1\n",
        &warnings
    );
    REQUIRE(look.has_value());
    CHECK(look->kinds.size() == 2); // by the map's kinds
    CHECK(warnings.size() == 1);    // only the typo'd top-level key
    std::vector<std::string> plain;
    const std::expected<core::WorldLook, core::Error> extra =
        Parse(With("  meadow:", "  snow: [terrain_snow_0]\n  meadow:"), &plain);
    REQUIRE(extra.has_value());
    CHECK(plain.empty());
    CHECK(extra->kinds.size() == 2);
}

TEST_CASE("an unknown key warns, at the top and in the bridge") {
    std::vector<std::string> warnings;
    const std::string text = With("down: bridge_v }", "down: bridge_v, over: x }") + "extras: 1\n";
    REQUIRE(Parse(text, &warnings).has_value());
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0] == std::format("world_look line {}: there is no key extras; it is ignored", LineOf(text, "extras:")));
    CHECK(warnings[1] == std::format("world_look line {}: bridge has no key over; it is ignored", LineOf(text, "bridge:")));
}

TEST_CASE("a look hashes by its sprites") {
    const std::expected<core::WorldLook, core::Error> first = Parse(SMALL_LOOK);
    const std::expected<core::WorldLook, core::Error> same = Parse(SMALL_LOOK);
    const std::expected<core::WorldLook, core::Error> other = Parse(With("terrain_meadow_1", "terrain_grass_1"));
    REQUIRE(first.has_value());
    REQUIRE(same.has_value());
    REQUIRE(other.has_value());
    CHECK(first->Hash() == same->Hash());
    CHECK(first->Hash() != other->Hash());
}

TEST_CASE("the shipped look loads clean against the shipped map and the small one") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    REQUIRE(assets.has_value());
    const std::expected<core::WorldLook, core::Error> small = data::LoadWorldLook(*assets / "data", *manifest, SmallMap());
    REQUIRE(small.has_value());
    CHECK(small->kinds.size() == 2);
    const std::expected<core::Strings, core::Error> strings = data::LoadStrings(*assets / "data");
    REQUIRE(strings.has_value());
    const auto world = data::LoadWorldMap(*assets / "data", *strings);
    REQUIRE(world.has_value());
    const std::expected<core::WorldLook, core::Error> shipped = data::LoadWorldLook(*assets / "data", *manifest, **world);
    REQUIRE(shipped.has_value());
    CHECK(shipped->kinds.size() == (*world)->Kinds().size());
    // LoadWorldLook logs its warnings; the same text parsed here hands them back, and there are none.
    std::ifstream input{ *assets / "data" / "world_look.yaml", std::ios::binary };
    const std::string text{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    std::vector<std::string> warnings;
    CHECK(data::ParseWorldLook(text, *manifest, **world, &warnings).has_value());
    CHECK(warnings.empty());
}

TEST_CASE("the shipped look covers the shipped map") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    REQUIRE(assets.has_value());
    const std::expected<core::Strings, core::Error> strings = data::LoadStrings(*assets / "data");
    REQUIRE(strings.has_value());
    const auto world = data::LoadWorldMap(*assets / "data", *strings);
    REQUIRE(world.has_value());
    const core::WorldMap& map = **world;
    const std::expected<core::WorldLook, core::Error> look = data::LoadWorldLook(*assets / "data", *manifest, map);
    REQUIRE(look.has_value());
    // Every kind the map declares has a look with a variant: the terrain draws no tile bare.
    REQUIRE(look->kinds.size() >= map.Kinds().size());
    for (std::size_t kind = 0; kind < map.Kinds().size(); ++kind) {
        CAPTURE(map.Kinds()[kind].name);
        CHECK_FALSE(look->kinds[kind].empty());
    }
    // Every sprite the look names is one the world scene loads, or the renderer would draw from a page that isn't resident.
    const std::optional<core::SceneId> worldScene = manifest->SceneNamed("world");
    REQUIRE(worldScene.has_value());
    const core::SceneId worldId = worldScene.value_or(core::SceneId{});
    const std::vector<core::atlas::SpriteId>& resident = manifest->Scene(worldId).sprites;
    std::vector<core::atlas::SpriteId> named = { look->bridgeAcross, look->bridgeDown, look->ford, look->cityMarker };
    named.insert(named.end(), look->roads.begin(), look->roads.end());
    named.insert(named.end(), look->rivers.begin(), look->rivers.end());
    for (const std::vector<core::atlas::SpriteId>& variants : look->kinds) {
        named.insert(named.end(), variants.begin(), variants.end());
    }
    for (const auto& [sort, sprite] : look->dangerMarkers) {
        named.push_back(sprite);
    }
    CHECK(std::ranges::all_of(named, [&](core::atlas::SpriteId sprite) { return std::ranges::contains(resident, sprite); }));
    // Every danger sort the map places has a marker.
    for (const core::Place& place : map.Places()) {
        if (place.kind == core::PlaceKind::Danger) {
            CAPTURE(place.key);
            CHECK(
                std::ranges::contains(look->dangerMarkers, place.sort, &std::pair<std::string, core::atlas::SpriteId>::first)
            );
        }
    }
}

} // namespace
