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
route: { dot: route_dot, spacing: 40, marker: route_marker, cursor: target_tile }
fog:
  clouds: [cloud_0, cloud_1, cloud_2]
  cloud_width: 1.2
  bob: { amplitude: 0.04, period: 5.0 }
  drift: 0.25
  fade_seconds: 0.6
  drain: { strength: 0.8, sepia: [150, 130, 100] }
  mist: { colour: [240, 230, 220], strength: 0.3 }
  shade: [210, 200, 190]
)yaml";

constexpr std::size_t MASKS = 16;
constexpr f32 SMALL_SPACING = 40.0F; // the small look's route spacing, not the default

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
    CHECK(look->route.dot == core::atlas::SpriteId::RouteDot);
    CHECK(look->route.spacing == SMALL_SPACING);
    CHECK(look->route.marker == core::atlas::SpriteId::RouteMarker);
    CHECK(look->route.cursor == core::atlas::SpriteId::TargetTile);
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
        { .why = "a route sprite the atlas lacks",
          .text = With("dot: route_dot", "dot: route_nowhere"),
          .says = std::format(
              "world_look line {}: route dot names the sprite \"route_nowhere\", which isn't in the atlas",
              LineOf(SMALL_LOOK, "route:")
          ) },
        { .why = "a route sprite the world scene doesn't load",
          .text = With("cursor: target_tile", "cursor: cell_plain"),
          .says = std::format(
              "world_look line {}: route cursor names the sprite \"cell_plain\", which the world scene doesn't load",
              LineOf(SMALL_LOOK, "route:")
          ) },
        { .why = "a route spacing of zero",
          .text = With("spacing: 40", "spacing: 0"),
          .says = std::format(
              "world_look line {}: route's spacing isn't a number above 0 and up to 4096", LineOf(SMALL_LOOK, "route:")
          ) },
        { .why = "a route spacing past the limit",
          .text = With("spacing: 40", "spacing: 1e30"),
          .says = std::format(
              "world_look line {}: route's spacing isn't a number above 0 and up to 4096", LineOf(SMALL_LOOK, "route:")
          ) },
        { .why = "a missing route",
          .text = With("route: { dot: route_dot, spacing: 40, marker: route_marker, cursor: target_tile }\n", ""),
          .says = "world_look line 1: the look has no route" },
        { .why = "a route with no marker",
          .text = With(" marker: route_marker,", ""),
          .says = std::format("world_look line {}: route has no marker", LineOf(SMALL_LOOK, "route:")) },
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

// The shipped look, loaded against the shipped map.
struct Shipped {
    std::shared_ptr<const core::Manifest> manifest;
    std::shared_ptr<const core::WorldMap> map;
    core::WorldLook look;
};

Shipped ShippedLook() {
    Shipped shipped{ .manifest = test::TestManifest(), .map = nullptr, .look = {} };
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    REQUIRE(assets.has_value());
    const std::expected<core::Strings, core::Error> strings = data::LoadStrings(*assets / "data");
    REQUIRE(strings.has_value());
    const auto world = data::LoadWorldMap(*assets / "data", *strings);
    REQUIRE(world.has_value());
    shipped.map = *world;
    std::expected<core::WorldLook, core::Error> look = data::LoadWorldLook(*assets / "data", *shipped.manifest, *shipped.map);
    REQUIRE(look.has_value());
    shipped.look = std::move(*look);
    return shipped;
}

TEST_CASE("the shipped look covers the shipped map") {
    const Shipped shipped = ShippedLook();
    const std::shared_ptr<const core::Manifest>& manifest = shipped.manifest;
    const core::WorldMap& map = *shipped.map;
    const core::WorldLook& look = shipped.look;
    // Every kind the map declares has a look with a variant: the terrain draws no tile bare.
    REQUIRE(look.kinds.size() >= map.Kinds().size());
    for (std::size_t kind = 0; kind < map.Kinds().size(); ++kind) {
        CAPTURE(map.Kinds()[kind].name);
        CHECK_FALSE(look.kinds[kind].empty());
    }
    // Every sprite the look names is one the world scene loads, or the renderer would draw from a page that isn't resident.
    const std::optional<core::SceneId> worldScene = manifest->SceneNamed("world");
    REQUIRE(worldScene.has_value());
    const core::SceneId worldId = worldScene.value_or(core::SceneId{});
    const std::vector<core::atlas::SpriteId>& resident = manifest->Scene(worldId).sprites;
    std::vector<core::atlas::SpriteId> named = { look.bridgeAcross, look.bridgeDown, look.ford, look.cityMarker };
    named.insert(named.end(), look.roads.begin(), look.roads.end());
    named.insert(named.end(), look.rivers.begin(), look.rivers.end());
    for (const std::vector<core::atlas::SpriteId>& variants : look.kinds) {
        named.insert(named.end(), variants.begin(), variants.end());
    }
    for (const auto& [sort, sprite] : look.dangerMarkers) {
        named.push_back(sprite);
    }
    CHECK(std::ranges::all_of(named, [&](core::atlas::SpriteId sprite) { return std::ranges::contains(resident, sprite); }));
    // Every danger sort the map places has a marker.
    for (const core::Place& place : map.Places()) {
        if (place.kind == core::PlaceKind::Danger) {
            CAPTURE(place.key);
            CHECK(std::ranges::contains(look.dangerMarkers, place.sort, &std::pair<std::string, core::atlas::SpriteId>::first));
        }
    }
}

TEST_CASE("a small look's fog reads the written values") {
    const std::expected<core::WorldLook, core::Error> look = Parse(SMALL_LOOK);
    REQUIRE(look.has_value());
    const core::FogLook& fog = look->fog;
    CHECK(
        fog.clouds == std::vector{ core::atlas::SpriteId::Cloud0, core::atlas::SpriteId::Cloud1, core::atlas::SpriteId::Cloud2 }
    );
    CHECK(fog.cloudWidth == doctest::Approx(1.2F));
    CHECK(fog.bobAmplitude == doctest::Approx(0.04F));
    CHECK(fog.bobPeriod == doctest::Approx(5.0F));
    CHECK(fog.drift == doctest::Approx(0.25F));
    CHECK(fog.fadeSeconds == doctest::Approx(0.6F));
    CHECK(fog.drain == doctest::Approx(0.8F));
    CHECK(fog.sepia == std::array<u8, 3>{ 150, 130, 100 });
    CHECK(fog.mist == std::array<u8, 3>{ 240, 230, 220 });
    CHECK(fog.mistStrength == doctest::Approx(0.3F));
    CHECK(fog.shade == std::array<u8, 3>{ 210, 200, 190 });
}

TEST_CASE("the shipped look's fog has 16 clouds and the written values") {
    const core::FogLook fog = ShippedLook().look.fog;
    REQUIRE(fog.clouds.size() == 16);
    CHECK(fog.clouds.front() == core::atlas::SpriteId::Cloud0);
    CHECK(fog.clouds.back() == core::atlas::SpriteId::Cloud15);
    CHECK(fog.cloudWidth == doctest::Approx(1.15F));
    CHECK(fog.bobAmplitude == doctest::Approx(0.03F));
    CHECK(fog.bobPeriod == doctest::Approx(4.0F));
    CHECK(fog.drift == doctest::Approx(0.3F));
    CHECK(fog.fadeSeconds == doctest::Approx(0.5F));
    CHECK(fog.drain == doctest::Approx(0.85F));
    CHECK(fog.sepia == std::array<u8, 3>{ 160, 138, 106 });
    CHECK(fog.mist == std::array<u8, 3>{ 239, 233, 220 });
    CHECK(fog.mistStrength == doctest::Approx(0.25F));
    CHECK(fog.shade == std::array<u8, 3>{ 214, 206, 192 });
}

TEST_CASE("a look without fog is refused") {
    std::string text{ SMALL_LOOK };
    text.erase(text.find("fog:"));
    const std::expected<core::WorldLook, core::Error> look = Parse(text);
    REQUIRE_FALSE(look.has_value());
    CHECK(look.error().message == "world_look line 1: the look has no fog");
}

TEST_CASE("a fog cloud outside the world scene's sprites is refused") {
    const std::string text = With("[cloud_0, cloud_1, cloud_2]", "[cloud_0, cell_plain]");
    const std::expected<core::WorldLook, core::Error> look = Parse(text);
    REQUIRE_FALSE(look.has_value());
    CHECK(
        look.error().message ==
        std::format(
            "world_look line {}: fog clouds names the sprite \"cell_plain\", which the world scene doesn't load",
            LineOf(text, "cell_plain")
        )
    );
}

TEST_CASE("fog values out of range are refused, each naming its key") {
    struct Broken {
        std::string_view why;
        std::string text;
        std::string key;
    };
    const std::vector<Broken> cases{
        { .why = "no clouds", .text = With("[cloud_0, cloud_1, cloud_2]", "[]"), .key = "fog clouds has no sprite" },
        { .why = "a cloud width of zero", .text = With("cloud_width: 1.2", "cloud_width: 0"), .key = "fog's cloud_width" },
        { .why = "a bob period of zero", .text = With("period: 5.0", "period: 0"), .key = "bob's period" },
        { .why = "a bob amplitude past a half", .text = With("amplitude: 0.04", "amplitude: 0.6"), .key = "bob's amplitude" },
        { .why = "a drift past two tiles", .text = With("drift: 0.25", "drift: 3"), .key = "fog's drift" },
        { .why = "a fade of zero", .text = With("fade_seconds: 0.6", "fade_seconds: 0"), .key = "fog's fade_seconds" },
        { .why = "a drain strength of 1.5", .text = With("strength: 0.8", "strength: 1.5"), .key = "drain's strength" },
        { .why = "a mist strength of 1.5", .text = With("strength: 0.3", "strength: 1.5"), .key = "mist's strength" },
        { .why = "a sepia of 256", .text = With("[150, 130, 100]", "[256, 130, 100]"), .key = "drain's sepia" },
        { .why = "a mist colour of two numbers", .text = With("[240, 230, 220]", "[240, 230]"), .key = "mist's colour" },
        { .why = "a shade of 256", .text = With("[210, 200, 190]", "[210, 200, 256]"), .key = "fog's shade" },
        { .why = "no drain",
          .text = With("  drain: { strength: 0.8, sepia: [150, 130, 100] }\n", ""),
          .key = "fog has no drain" },
    };
    for (const Broken& broken : cases) {
        CAPTURE(broken.why);
        const std::expected<core::WorldLook, core::Error> look = Parse(broken.text);
        REQUIRE_FALSE(look.has_value());
        CHECK(look.error().message.starts_with("world_look line "));
        CHECK(look.error().message.contains(broken.key));
    }
}

TEST_CASE("the look's hash changes with a fog value") {
    const std::expected<core::WorldLook, core::Error> first = Parse(SMALL_LOOK);
    const std::expected<core::WorldLook, core::Error> drift = Parse(With("drift: 0.25", "drift: 0.5"));
    const std::expected<core::WorldLook, core::Error> shade = Parse(With("[210, 200, 190]", "[210, 200, 191]"));
    const std::expected<core::WorldLook, core::Error> clouds = Parse(With("cloud_2]", "cloud_3]"));
    REQUIRE(first.has_value());
    REQUIRE(drift.has_value());
    REQUIRE(shade.has_value());
    REQUIRE(clouds.has_value());
    CHECK(first->Hash() != drift->Hash());
    CHECK(first->Hash() != shade->Hash());
    CHECK(first->Hash() != clouds->Hash());
}

TEST_CASE("an unknown key in the fog's sections warns") {
    std::vector<std::string> warnings;
    const std::string text = With("period: 5.0 }", "period: 5.0, wobble: 1 }");
    REQUIRE(Parse(text, &warnings).has_value());
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == std::format("world_look line {}: bob has no key wobble; it is ignored", LineOf(text, "bob:")));
    std::vector<std::string> tints;
    const std::string drain = With("sepia: [150, 130, 100] }", "sepia: [150, 130, 100], tone: 1 }");
    REQUIRE(Parse(drain, &tints).has_value());
    REQUIRE(tints.size() == 1);
    CHECK(tints[0].contains("drain has no key tone"));
}

} // namespace
