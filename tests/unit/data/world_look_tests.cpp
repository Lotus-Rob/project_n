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
  deep_water:
    wash: [88, 124, 148]
    strength: 0.68
    stamps: { sprites: [stamp_water_0, stamp_water_1], density: 0.08, size: [0.7, 0.9], jitter: 0.15, thin_below: 0.5, mist: false }
  meadow:
    wash: [196, 204, 146]
    strength: 0.1
    stamps: { sprites: [stamp_meadow_0], density: 0.03, size: [0.6, 0.9], jitter: 0.2, thin_below: 0, mist: true }
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
ground:
  paper: [240, 235, 222]
  grain: 0.04
  edge_warp: 0.45
  edge_soft: 0.5
  blotch: 0.5
  water: { shallow: meadow, deep: deep_water, depth_tiles: 2.5 }
  shore: { colour: [70, 104, 112], strength: 0.55, width: 0.12 }
  grid: { colour: [90, 80, 70], strength: 0.09, fade: [0.6, 0.35] }
veil: { colour: [246, 244, 238], scale: 3.0, strength: 0.55 }
mist: { sprites: [mist_0, mist_1], colour: [246, 244, 238], strength: 0.9, chance: 0.7, width: [1.0, 1.4] }
river: { width: 0.38, edge: 0.5, colour: [128, 172, 176], edge_colour: [70, 104, 112], edge_strength: 0.67 }
road: { width: 0.26, edge: 0.4, colour: [132, 112, 88], edge_colour: [104, 90, 72], edge_strength: 0.4 }
places:
  city: painting_city
  dangers: { mountain_pass: painting_mountain_pass }
  by_key: { capital: painting_capital }
)yaml";

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

// A kind called `name` with the stamp sprites `sprites`, to put in the look's `kinds:` before meadow.
std::string ExtraKind(std::string_view name, std::string_view sprites) {
    return std::format(
        "  {}:\n    wash: [236, 239, 242]\n    strength: 0.2\n"
        "    stamps: {{ sprites: {}, density: 0.3, size: [1.2, 1.7], jitter: 0.3, thin_below: 0, mist: true }}\n",
        name,
        sprites
    );
}

// SMALL_LOOK with the kind `snow` added.
std::string WithSnow(std::string_view sprites) {
    return With("  meadow:\n", ExtraKind("snow", sprites) + "  meadow:\n");
}

// SMALL_LOOK without its meadow kind.
std::string WithoutMeadow() {
    std::string text{ SMALL_LOOK };
    const std::size_t first = text.find("  meadow:\n");
    const std::size_t last = text.find("bridge:");
    REQUIRE(first != std::string::npos);
    REQUIRE(last != std::string::npos);
    return text.erase(first, last - first);
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
    CHECK(
        look->kinds[0].stamps.sprites == std::vector{ core::atlas::SpriteId::StampWater0, core::atlas::SpriteId::StampWater1 }
    );
    CHECK(look->kinds[1].stamps.sprites == std::vector{ core::atlas::SpriteId::StampMeadow0 });
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
    const std::string noMeadow = WithoutMeadow();
    const std::string noPass = With("  mountain_pass: marker_mountain_pass\n", "");
    const std::string badSprite = With("[stamp_water_0,", "[stamp_lava_0,");
    const std::vector<Broken> cases{
        { .why = "a kind of the map with no look",
          .text = noMeadow,
          .says = std::format(
              "world_look line {}: the map's kind meadow has no look in kinds", LineOf(SMALL_LOOK, "deep_water:")
          ) },
        { .why = "a danger sort the map uses with no marker",
          .text = noPass,
          .says = std::format(
              "world_look line {}: the map's danger sort mountain_pass has no marker in markers", LineOf(SMALL_LOOK, "city:")
          ) },
        { .why = "a sprite the manifest lacks",
          .text = badSprite,
          .says = std::format(
              "world_look line {}: kind deep_water stamps sprites names the sprite \"stamp_lava_0\", which isn't in the atlas",
              LineOf(SMALL_LOOK, "stamps: { sprites: [stamp_water_0")
          ) },
        { .why = "an unused kind naming a sprite the manifest lacks",
          .text = WithSnow("[stamp_lava_0]"),
          .says = std::format(
              "world_look line {}: kind snow stamps sprites names the sprite \"stamp_lava_0\", which isn't in the atlas",
              LineOf(WithSnow("[stamp_lava_0]"), "stamps: { sprites: [stamp_lava_0]")
          ) },
        { .why = "an unused marker naming a sprite the manifest lacks",
          .text = With("  mountain_pass:", "  cave: marker_nowhere\n  mountain_pass:"),
          .says = std::format(
              "world_look line {}: marker cave names the sprite \"marker_nowhere\", which isn't in the atlas",
              LineOf(With("  mountain_pass:", "  cave: marker_nowhere\n  mountain_pass:"), "cave:")
          ) },
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
          .text = WithSnow("[cell_snow]"),
          .says = std::format(
              "world_look line {}: kind snow stamps sprites names the sprite \"cell_snow\", which the world scene doesn't "
              "load",
              LineOf(WithSnow("[cell_snow]"), "stamps: { sprites: [cell_snow]")
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
    const std::expected<core::WorldLook, core::Error> extra = Parse(WithSnow("[stamp_snow_0]"), &plain);
    REQUIRE(extra.has_value());
    CHECK(plain.empty());
    CHECK(extra->kinds.size() == 2);
}

TEST_CASE("a kind with variants warns that the key is unknown, and the look still reads") {
    std::vector<std::string> warnings;
    const std::string text = With("    strength: 0.68\n", "    strength: 0.68\n    variants: [stamp_water_0]\n");
    const std::expected<core::WorldLook, core::Error> look = Parse(text, &warnings);
    REQUIRE(look.has_value());
    REQUIRE(warnings.size() == 1);
    CHECK(
        warnings[0] ==
        std::format("world_look line {}: kind deep_water has no key variants; it is ignored", LineOf(text, "variants:"))
    );
    CHECK(look->kinds[0].stamps.sprites.front() == core::atlas::SpriteId::StampWater0);
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
    const std::expected<core::WorldLook, core::Error> other =
        Parse(With("[stamp_water_0, stamp_water_1]", "[stamp_water_0, stamp_water_2]"));
    REQUIRE(first.has_value());
    REQUIRE(same.has_value());
    REQUIRE(other.has_value());
    CHECK(first->Hash() == same->Hash());
    CHECK(first->Hash() != other->Hash());
}

TEST_CASE("the shipped look loads clean against the shipped map and is refused by a map lacking its water kinds") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    REQUIRE(assets.has_value());
    const std::expected<core::WorldLook, core::Error> small = data::LoadWorldLook(*assets / "data", *manifest, SmallMap());
    REQUIRE_FALSE(small.has_value());
    CHECK(small.error().message.starts_with("world_look line "));
    CHECK(small.error().message.contains("water's shallow \"shallows\" isn't a kind of the map"));
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
    REQUIRE_MESSAGE(look.has_value(), look.error().message);
    shipped.look = std::move(*look);
    return shipped;
}

TEST_CASE("the shipped look covers the shipped map") {
    const Shipped shipped = ShippedLook();
    const std::shared_ptr<const core::Manifest>& manifest = shipped.manifest;
    const core::WorldMap& map = *shipped.map;
    const core::WorldLook& look = shipped.look;
    // Every kind the map declares has a look with stamps to pick from.
    REQUIRE(look.kinds.size() >= map.Kinds().size());
    for (std::size_t kind = 0; kind < map.Kinds().size(); ++kind) {
        CAPTURE(map.Kinds()[kind].name);
        CHECK_FALSE(look.kinds[kind].stamps.sprites.empty());
    }
    // Every sprite the look names is one the world scene loads, or the renderer would draw from a page that isn't resident.
    const std::optional<core::SceneId> worldScene = manifest->SceneNamed("world");
    REQUIRE(worldScene.has_value());
    const core::SceneId worldId = worldScene.value_or(core::SceneId{});
    const std::vector<core::atlas::SpriteId>& resident = manifest->Scene(worldId).sprites;
    std::vector<core::atlas::SpriteId> named = { look.bridgeAcross, look.bridgeDown, look.ford, look.cityMarker };
    for (const core::KindLook& kind : look.kinds) {
        named.insert(named.end(), kind.stamps.sprites.begin(), kind.stamps.sprites.end());
    }
    named.insert(named.end(), look.mist.sprites.begin(), look.mist.sprites.end());
    named.push_back(look.places.city);
    for (const auto& [sort, sprite] : look.places.dangers) {
        named.push_back(sprite);
    }
    for (const auto& [key, sprite] : look.places.byKey) {
        named.push_back(sprite);
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
    CHECK(fog.sepia == std::array<u8, 3>{ 160, 148, 124 });
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

TEST_CASE("road reads as written") {
    const Shipped shipped = ShippedLook();
    const core::RibbonLook& road = shipped.look.road;
    CHECK(road.width == doctest::Approx(0.26F));
    CHECK(road.edge == doctest::Approx(0.4F));
    CHECK(road.colour == std::array<u8, 3>{ { 149, 132, 109 } });
    CHECK(road.edgeColour == std::array<u8, 3>{ { 126, 113, 96 } });
    CHECK(road.edgeStrength == doctest::Approx(0.4F));
    // A road is a narrower line than a river.
    CHECK(road.width < shipped.look.river.width);
    const std::expected<core::WorldLook, core::Error> small = Parse(SMALL_LOOK);
    REQUIRE(small.has_value());
    CHECK(small->road.width == doctest::Approx(0.26F));
}

TEST_CASE("a ribbon's taper is read in tiles, 0 when absent") {
    const std::expected<core::WorldLook, core::Error> tapered =
        Parse(With("edge_strength: 0.67 }", "edge_strength: 0.67, taper: 1.5 }"));
    REQUIRE(tapered.has_value());
    CHECK(tapered->river.taper == 1.5F);
    CHECK(tapered->road.taper == 0.0F);
    for (const std::string_view bad : { "taper: 5", "taper: -1" }) {
        CAPTURE(bad);
        const std::expected<core::WorldLook, core::Error> look =
            Parse(With("edge_strength: 0.67 }", std::format("edge_strength: 0.67, {} }}", bad)));
        REQUIRE_FALSE(look.has_value());
        CHECK(look.error().message.contains("taper"));
    }
}

TEST_CASE("the shipped look's ground, veil, mist, river and places read as written") {
    const Shipped shipped = ShippedLook();
    const core::WorldLook& look = shipped.look;
    const std::span<const core::TerrainKind> kinds = shipped.map->Kinds();
    const auto indexOf = [&](std::string_view name) {
        return static_cast<u8>(std::ranges::find(kinds, name, &core::TerrainKind::name) - kinds.begin());
    };
    CHECK(look.ground.paper == std::array<u8, 3>{ { 240, 235, 222 } });
    CHECK(look.ground.grain == doctest::Approx(0.04F));
    CHECK(look.ground.edgeWarp == doctest::Approx(0.45F));
    CHECK(look.ground.edgeSoft == doctest::Approx(0.5F));
    CHECK(look.ground.blotch == doctest::Approx(0.5F));
    CHECK(look.ground.shallowKind == indexOf("shallows"));
    CHECK(look.ground.deepKind == indexOf("deep_water"));
    CHECK(look.ground.depthTiles == doctest::Approx(3.0F));
    CHECK(look.ground.shore == std::array<u8, 3>{ { 70, 104, 112 } });
    CHECK(look.ground.shoreStrength == doctest::Approx(0.55F));
    CHECK(look.ground.shoreWidth == doctest::Approx(0.12F));
    CHECK(look.ground.grid == std::array<u8, 3>{ { 90, 80, 70 } });
    CHECK(look.ground.gridStrength == doctest::Approx(0.09F));
    CHECK(look.ground.gridFade[0] == doctest::Approx(0.6F));
    CHECK(look.ground.gridFade[1] == doctest::Approx(0.35F));
    CHECK(look.veil.colour == std::array<u8, 3>{ { 246, 244, 238 } });
    CHECK(look.veil.scale == doctest::Approx(3.0F));
    CHECK(look.veil.strength == doctest::Approx(0.55F));
    REQUIRE(look.mist.sprites.size() == 4);
    CHECK(look.mist.sprites.front() == core::atlas::SpriteId::Mist0);
    CHECK(look.mist.strength == doctest::Approx(0.9F));
    CHECK(look.mist.chance == doctest::Approx(1.0F));
    CHECK(look.mist.width[0] == doctest::Approx(1.0F));
    CHECK(look.mist.width[1] == doctest::Approx(1.4F));
    CHECK(look.river.width == doctest::Approx(0.38F));
    CHECK(look.river.edge == doctest::Approx(0.5F));
    CHECK(look.river.colour == std::array<u8, 3>{ { 128, 172, 176 } });
    CHECK(look.river.edgeColour == std::array<u8, 3>{ { 70, 104, 112 } });
    CHECK(look.river.edgeStrength == doctest::Approx(0.67F));
    CHECK(look.places.city == core::atlas::SpriteId::PaintingCity);
    CHECK(look.places.dangers.size() == 6);
    REQUIRE(look.places.byKey.size() == 1);
    CHECK(look.places.byKey[0].first == "capital");
    CHECK(look.places.byKey[0].second == core::atlas::SpriteId::PaintingCapital);
}

TEST_CASE("the shipped forest's stamps read as written") {
    const Shipped shipped = ShippedLook();
    const std::span<const core::TerrainKind> kinds = shipped.map->Kinds();
    const std::size_t forest =
        static_cast<std::size_t>(std::ranges::find(kinds, "forest", &core::TerrainKind::name) - kinds.begin());
    REQUIRE(forest < shipped.look.kinds.size());
    const core::KindLook& kind = shipped.look.kinds[forest];
    CHECK(kind.wash == std::array<u8, 3>{ { 66, 102, 74 } });
    CHECK(kind.strength == doctest::Approx(0.12F));
    REQUIRE(kind.stamps.sprites.size() == 4);
    CHECK(kind.stamps.sprites.front() == core::atlas::SpriteId::StampForest0);
    CHECK(kind.stamps.sprites.back() == core::atlas::SpriteId::StampForest3);
    CHECK(kind.stamps.density == doctest::Approx(0.45F));
    CHECK(kind.stamps.size[0] == doctest::Approx(1.0F));
    CHECK(kind.stamps.size[1] == doctest::Approx(1.3F));
    CHECK(kind.stamps.jitter == doctest::Approx(0.15F));
    CHECK(kind.stamps.thinBelow == doctest::Approx(0.0F));
    CHECK(kind.stamps.mist);
}

TEST_CASE("a look with more kinds than the ground's table is refused") {
    std::string extras;
    for (u32 index = 0; index < core::MAX_GROUND_KINDS - 2; ++index) {
        extras += ExtraKind(std::format("spare_{}", index), "[stamp_snow_0]");
    }
    // Two kinds of the map and 14 spares fill the table; one more is refused.
    CHECK(Parse(With("  meadow:\n", extras + "  meadow:\n")).has_value());
    const std::string text = With("  meadow:\n", extras + ExtraKind("spare_last", "[stamp_snow_0]") + "  meadow:\n");
    const std::expected<core::WorldLook, core::Error> look = Parse(text);
    REQUIRE_FALSE(look.has_value());
    CHECK(look.error().message.starts_with(std::format("world_look line {}: ", LineOf(text, "deep_water:"))));
    CHECK(look.error().message.contains("more than the 16"));
}

TEST_CASE("a map kind missing from the look is refused, naming it") {
    const std::expected<core::WorldLook, core::Error> look = Parse(WithoutMeadow());
    REQUIRE_FALSE(look.has_value());
    CHECK(look.error().message.contains("the map's kind meadow has no look"));
}

TEST_CASE("ground values out of range are refused, each naming its key") {
    struct Broken {
        std::string_view why;
        std::string text;
        std::string key;
    };
    const std::vector<Broken> cases{
        { .why = "a kind strength of 1.5",
          .text = With("strength: 0.68", "strength: 1.5"),
          .key = "kind deep_water's strength" },
        { .why = "a wash of 256",
          .text = With("wash: [88, 124, 148]", "wash: [256, 124, 148]"),
          .key = "kind deep_water's wash" },
        { .why = "stamp sizes out of order", .text = With("size: [0.7, 0.9]", "size: [0.9, 0.6]"), .key = "stamps's size" },
        { .why = "a stamp size past four tiles", .text = With("size: [0.7, 0.9]", "size: [0.7, 4.5]"), .key = "stamps's size" },
        { .why = "a stamp size of zero", .text = With("size: [0.7, 0.9]", "size: [0, 0.9]"), .key = "stamps's size" },
        { .why = "a stamp density of 1.5", .text = With("density: 0.08", "density: 1.5"), .key = "stamps's density" },
        { .why = "a stamp jitter of 2", .text = With("jitter: 0.15", "jitter: 2"), .key = "stamps's jitter" },
        { .why = "a thin_below of 3", .text = With("thin_below: 0.5", "thin_below: 3"), .key = "stamps's thin_below" },
        { .why = "a mist flag that isn't one", .text = With("mist: false", "mist: maybe"), .key = "stamps's mist" },
        { .why = "no stamps",
          .text = With("sprites: [stamp_water_0, stamp_water_1]", "sprites: []"),
          .key = "stamps sprites has no sprite" },
        { .why = "a depth of zero", .text = With("depth_tiles: 2.5", "depth_tiles: 0"), .key = "water's depth_tiles" },
        { .why = "a depth past four", .text = With("depth_tiles: 2.5", "depth_tiles: 4.5"), .key = "water's depth_tiles" },
        { .why = "a depth the shore distance can't reach",
          .text = With("depth_tiles: 2.5", "depth_tiles: 3.6"),
          .key = "water's depth_tiles" },
        { .why = "a water kind the map lacks", .text = With("shallow: meadow", "shallow: lava"), .key = "water's shallow" },
        { .why = "a paper of 256", .text = With("paper: [240, 235, 222]", "paper: [240, 235, 256]"), .key = "ground's paper" },
        { .why = "a blotch of 1.5", .text = With("blotch: 0.5", "blotch: 1.5"), .key = "ground's blotch" },
        { .why = "an edge_soft of zero", .text = With("edge_soft: 0.5", "edge_soft: 0"), .key = "ground's edge_soft" },
        { .why = "an edge_soft past a tile", .text = With("edge_soft: 0.5", "edge_soft: 1.5"), .key = "ground's edge_soft" },
        { .why = "a grid fade that rises", .text = With("fade: [0.6, 0.35]", "fade: [0.35, 0.6]"), .key = "grid's fade" },
        { .why = "a grid fade past two", .text = With("fade: [0.6, 0.35]", "fade: [2.5, 0.35]"), .key = "grid's fade" },
        { .why = "a shore width of zero", .text = With("width: 0.12", "width: 0"), .key = "shore's width" },
        { .why = "a veil scale of zero", .text = With("scale: 3.0", "scale: 0"), .key = "veil's scale" },
        { .why = "a mist width out of order", .text = With("width: [1.0, 1.4]", "width: [1.4, 1.0]"), .key = "mist's width" },
        { .why = "a mist width past two", .text = With("width: [1.0, 1.4]", "width: [1.0, 2.5]"), .key = "mist's width" },
        { .why = "water's two kinds the same", .text = With("deep: deep_water", "deep: meadow"), .key = "water's deep" },
        { .why = "a mist chance of 1.5", .text = With("chance: 0.7", "chance: 1.5"), .key = "mist's chance" },
        { .why = "a river edge strength of 1.5",
          .text = With("edge_strength: 0.67", "edge_strength: 1.5"),
          .key = "river's edge_strength" },
        { .why = "a river width of zero", .text = With("width: 0.38", "width: 0"), .key = "river's width" },
        { .why = "no river",
          .text = With(
              "river: { width: 0.38, edge: 0.5, colour: [128, 172, 176], edge_colour: [70, 104, 112], edge_strength: 0.67 }\n",
              ""
          ),
          .key = "the look has no river" },
        { .why = "a road edge narrower than its width",
          .text = With("edge: 0.4", "edge: 0.2"),
          .key = "road's edge is narrower than its width" },
        { .why = "a road width of zero", .text = With("width: 0.26", "width: 0"), .key = "road's width" },
        { .why = "no road",
          .text = With(
              "road: { width: 0.26, edge: 0.4, colour: [132, 112, 88], edge_colour: [104, 90, 72], edge_strength: 0.4 }\n", ""
          ),
          .key = "the look has no road" },
        { .why = "no ground", .text = With("ground:\n", "grounds:\n"), .key = "the look has no ground" },
    };
    for (const Broken& broken : cases) {
        CAPTURE(broken.why);
        const std::expected<core::WorldLook, core::Error> look = Parse(broken.text);
        REQUIRE_FALSE(look.has_value());
        CHECK(look.error().message.starts_with("world_look line "));
        CHECK(look.error().message.contains(broken.key));
    }
}

TEST_CASE("ground refusals name the line of the value") {
    const std::string size = With("size: [0.7, 0.9]", "size: [0.9, 0.6]");
    const std::string water = With("shallow: meadow", "shallow: lava");
    const std::string fade = With("fade: [0.6, 0.35]", "fade: [0.35, 0.6]");
    const std::string same = With("deep: deep_water", "deep: meadow");
    const std::vector<std::pair<std::string, u32>> cases{
        { size, LineOf(size, "size: [0.9, 0.6]") },
        { water, LineOf(water, "shallow: lava") },
        { fade, LineOf(fade, "fade: [0.35, 0.6]") },
        { same, LineOf(same, "deep: meadow") },
    };
    for (const auto& [text, line] : cases) {
        const std::expected<core::WorldLook, core::Error> look = Parse(text);
        REQUIRE_FALSE(look.has_value());
        CHECK(look.error().message.starts_with(std::format("world_look line {}: ", line)));
    }
}

TEST_CASE("a painting for a place key no place has is refused") {
    const std::string text =
        With("by_key: { capital: painting_capital }", "by_key: { capital: painting_capital, nowhere: painting_cave }");
    const std::expected<core::WorldLook, core::Error> look = Parse(text);
    REQUIRE_FALSE(look.has_value());
    CHECK(look.error().message.starts_with(std::format("world_look line {}: ", LineOf(text, "by_key:"))));
    CHECK(look.error().message.contains("nowhere"));
}

TEST_CASE("a danger sort the map lacks is ignored and one it uses needs a painting") {
    std::vector<std::string> warnings;
    const std::expected<core::WorldLook, core::Error> extra =
        Parse(With("dangers: { mountain_pass:", "dangers: { cave: painting_cave, mountain_pass:"), &warnings);
    REQUIRE(extra.has_value());
    CHECK(warnings.empty());
    const std::expected<core::WorldLook, core::Error> missing =
        Parse(With("dangers: { mountain_pass: painting_mountain_pass }", "dangers: { cave: painting_cave }"));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().message.contains("the map's danger sort mountain_pass has no painting"));
}

TEST_CASE("the look's hash changes with a stamp density") {
    const std::expected<core::WorldLook, core::Error> first = Parse(SMALL_LOOK);
    REQUIRE(first.has_value());
    const std::vector<std::string> edits{
        With("density: 0.08", "density: 0.09"),
        With("wash: [88, 124, 148]", "wash: [88, 124, 149]"),
        With("paper: [240, 235, 222]", "paper: [240, 235, 221]"),
        With("depth_tiles: 2.5", "depth_tiles: 3.5"),
        With("scale: 3.0", "scale: 3.5"),
        With("chance: 0.7", "chance: 0.6"),
        With("edge: 0.5", "edge: 0.6"),
        With("city: painting_city", "city: painting_cave"),
        With("capital: painting_capital", "capital: painting_cave"),
        With("stamp_meadow_0]", "stamp_meadow_1]"),
    };
    for (const std::string& text : edits) {
        const std::expected<core::WorldLook, core::Error> edited = Parse(text);
        REQUIRE(edited.has_value());
        CHECK(first->Hash() != edited->Hash());
    }
}

} // namespace
