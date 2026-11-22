#include "core/cxx23.hpp" // IWYU pragma: keep
#include "data/strings.hpp"
#include "data/world_map.hpp"
#include "platform/paths.hpp"

#include <doctest/doctest.h>

namespace {

constexpr std::string_view SMALL = R"yaml(road_cost: 4
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

constexpr f32 TRAVEL_CULTIVATOR_POWER = 6.0F;
constexpr u32 COLUMNS = 5;
constexpr u32 ROWS = 3;
constexpr u32 PEOPLE = 100;
constexpr f32 TILE_SIZE = 256.0F;
constexpr u16 MEADOW_COST = 10;
constexpr u32 FORD_TILE = 8;
constexpr std::ptrdiff_t SHIPPED_CITIES = 6;
constexpr std::ptrdiff_t LEAST_DANGERS = 18;
constexpr std::ptrdiff_t MOST_DANGERS = 22;

core::Strings Names() {
    core::Strings strings;
    strings.Add("place_capital", { { "Capital", "都城" } });
    strings.Add("place_wolf_pass", { { "Wolf Pass", "狼关" } });
    return strings;
}

// assets/data/world.yaml read against the shipped strings, as main reads it (support/world.hpp's ShippedWorld() needs game,
// which data_tests doesn't link).
std::shared_ptr<const core::WorldMap> Shipped() {
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    REQUIRE(assets.has_value());
    const std::expected<core::Strings, core::Error> strings = data::LoadStrings(*assets / "data");
    REQUIRE(strings.has_value());
    std::expected<std::shared_ptr<const core::WorldMap>, core::Error> map = data::LoadWorldMap(*assets / "data", *strings);
    REQUIRE(map.has_value());
    return std::move(*map);
}

// SMALL with `from` replaced by `replacement`.
std::string With(std::string_view from, std::string_view replacement) {
    std::string text{ SMALL };
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

std::string WithCrlf(std::string text) {
    for (std::size_t at = text.find('\n'); at != std::string::npos; at = text.find('\n', at + 2)) {
        text.insert(at, "\r");
    }
    return text;
}

TEST_CASE("a small map parses into its kinds, lines and places") {
    const core::Strings strings = Names();
    const std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(SMALL, strings);
    REQUIRE(map.has_value());
    CHECK(map->Columns() == COLUMNS);
    CHECK(map->Rows() == ROWS);
    CHECK(map->TileSize() == doctest::Approx(TILE_SIZE));
    REQUIRE(map->Kinds().size() == 2);
    CHECK(map->Kinds()[0].name == "deep_water");
    CHECK(map->Kinds()[0].symbol == '~');
    CHECK(map->Kinds()[0].cost == core::IMPASSABLE_COST);
    CHECK(map->Kinds()[1].cost == MEADOW_COST);
    CHECK(map->KindAt(0) == 0);
    CHECK(map->KindAt(COLUMNS + 2) == 0);
    CHECK(map->KindAt(COLUMNS) == 1);
    CHECK(map->Roads().size() == 1);
    CHECK(map->Rivers().size() == 1);
    CHECK((map->FlagsAt(FORD_TILE) & static_cast<u8>(core::TileFlag::Ford)) != 0);
    REQUIRE(map->Places().size() == 2);
    const core::Place& capital = map->Places()[0];
    CHECK(capital.key == "capital");
    CHECK(capital.kind == core::PlaceKind::City);
    CHECK(capital.tile == core::TilePoint{ .column = 0, .row = 2 });
    CHECK(capital.people == PEOPLE);
    CHECK(capital.start);
    CHECK(capital.nameKey == "place_capital");
    CHECK(capital.name == strings.Find("place_capital"));
    CHECK(map->Places()[1].kind == core::PlaceKind::Danger);
    CHECK(map->Places()[1].sort == "mountain_pass");
    CHECK(map->Start() == 0);
}

TEST_CASE("a grid with CRLF line ends or a trailing space reads as the same grid") {
    const core::Strings strings = Names();
    const std::expected<core::WorldMap, core::Error> plain = data::ParseWorldMap(SMALL, strings);
    const std::expected<core::WorldMap, core::Error> crlf = data::ParseWorldMap(WithCrlf(std::string{ SMALL }), strings);
    const std::expected<core::WorldMap, core::Error> spaced = data::ParseWorldMap(With("..~..\n", "..~..  \n"), strings);
    REQUIRE(plain.has_value());
    REQUIRE(crlf.has_value());
    REQUIRE(spaced.has_value());
    CHECK(crlf->Hash() == plain->Hash());
    CHECK(spaced->Hash() == plain->Hash());
}

TEST_CASE("a tab in a row is refused, naming its line") {
    const std::string text = With("..~..\n", "..\t..\n");
    const std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(text, Names());
    REQUIRE_FALSE(map.has_value());
    const std::string atLine = std::format("line {}:", LineOf(text, "..\t.."));
    CHECK(map.error().message.contains(atLine));
}

// A change to SMALL that the reader must refuse, and what the refusal must name.
struct Broken {
    std::string_view from;
    std::string_view replacement;
    std::string_view onLineOf; // the text of SMALL (as changed) whose line the refusal names
    std::string_view says;
};

// Whether the reader refuses each of `cases`, naming the line and saying what each `says`.
template<std::size_t COUNT>
void CheckRefused(const std::array<Broken, COUNT>& cases) {
    const core::Strings strings = Names();
    for (const Broken& broken : cases) {
        CAPTURE(broken.from);
        const std::string text = With(broken.from, broken.replacement);
        const std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(text, strings);
        REQUIRE_FALSE(map.has_value());
        CHECK(map.error().message.contains(broken.says));
        const std::string atLine = std::format("line {}:", LineOf(text, broken.onLineOf));
        CHECK(map.error().message.contains(atLine));
    }
}

TEST_CASE("the reader refuses, naming the line") {
    constexpr std::array<Broken, 24> CASES{ {
        Broken{ .from = "..~..\n", .replacement = "..~.\n", .onLineOf = "..~.\n", .says = "row" },
        Broken{ .from = "..~..\n", .replacement = "..x..\n", .onLineOf = "..x..", .says = "'x'" },
        Broken{ .from = ", cost: 10", .replacement = "", .onLineOf = "name: meadow", .says = "cost" },
        Broken{ .from = "name: place_wolf_pass",
                .replacement = "name: place_nowhere",
                .onLineOf = "wolf_pass,",
                .says = "place_nowhere" },
        Broken{ .from = "kind: danger", .replacement = "kind: town", .onLineOf = "kind: town", .says = "town" },
        Broken{ .from = "sort: mountain_pass, ", .replacement = "", .onLineOf = "key: wolf_pass", .says = "sort" },
        Broken{ .from = "tile: [4, 1]", .replacement = "tile: [4, 9]", .onLineOf = "key: wolf_pass", .says = "off the map" },
        Broken{ .from = "fords: [ [3, 1] ]", .replacement = "fords: [ [2, 1] ]", .onLineOf = "fords:", .says = "ford" },
        Broken{ .from = "road_cost: 4", .replacement = "road_cost: 5000", .onLineOf = "road_cost", .says = "road_cost" },
        Broken{ .from = "ford_cost: 30", .replacement = "ford_cost: 5000", .onLineOf = "ford_cost", .says = "ford_cost" },
        Broken{ .from = "tile_size: 256", .replacement = "tile_size: 0", .onLineOf = "tile_size", .says = "tile_size" },
        Broken{ .from = "[0, 2], [4, 2]", .replacement = "[0, 2], [4, 9]", .onLineOf = "roads:", .says = "road" },
        Broken{ .from = "symbol: \".\"", .replacement = "symbol: \"~\"", .onLineOf = "name: meadow", .says = "symbol" },
        Broken{ .from = ", cost: 10", .replacement = ", cost: 2000", .onLineOf = "name: meadow", .says = "2000" },
        Broken{ .from = "[[3, 1], [3, 2]]", .replacement = "[[3, 1], [3, 9]]", .onLineOf = "rivers:", .says = "river" },
        Broken{ .from = "tile_size: 256", .replacement = "tile_size: 1e-50", .onLineOf = "tile_size", .says = "tile_size" },
        Broken{ .from = "grid: |", .replacement = "gridx: |", .onLineOf = "road_cost", .says = "grid" },
        Broken{ .from = "sort: mountain_pass, ",
                .replacement = "sort: mountain_pass, people: 5, ",
                .onLineOf = "key: wolf_pass",
                .says = "danger with people" },
        Broken{
            .from = "name: meadow", .replacement = "name: deep_water", .onLineOf = "symbol: \".\"", .says = "share the name" },
        Broken{ .from = ", cost: 10", .replacement = ", cost: 0", .onLineOf = "name: meadow", .says = "is 0" },
        Broken{ .from = "road_cost: 4", .replacement = "road_cost: 0", .onLineOf = "road_cost", .says = "road_cost is 0" },
        Broken{ .from = "ford_cost: 30", .replacement = "ford_cost: 0", .onLineOf = "ford_cost", .says = "ford_cost is 0" },
        Broken{ .from = "ticks_per_cost: 16",
                .replacement = "ticks_per_cost: 16, kinds: { meadow: 0 }",
                .onLineOf = "ticks_per_cost",
                .says = "meadow cost is 0" },
        Broken{ .from = "ticks_per_cost: 16",
                .replacement = "ticks_per_cost: 16, features: { ford: 0 }",
                .onLineOf = "ticks_per_cost",
                .says = "ford cost is 0" },
    } };
    CheckRefused(CASES);
}

TEST_CASE("the reader refuses bad profiles, draws and travel, naming the line") {
    constexpr std::array<Broken, 18> CASES{ {
        Broken{ .from = "profiles:\n  - { key: on_foot, ticks_per_cost: 16 }\n",
                .replacement = "",
                .onLineOf = "road_cost",
                .says = "no profiles" },
        Broken{ .from = "ticks_per_cost: 16",
                .replacement = "ticks_per_cost: 0",
                .onLineOf = "ticks_per_cost",
                .says = "no ticks" },
        Broken{ .from = "ticks_per_cost: 16",
                .replacement = "ticks_per_cost: 16, kinds: { lava: 3 }",
                .onLineOf = "ticks_per_cost",
                .says = "lava" },
        Broken{ .from = "ford_cost: 30",
                .replacement = "ford_cost: 30\ndanger_reach: 17",
                .onLineOf = "danger_reach",
                .says = "danger_reach" },
        Broken{ .from = "ticks_per_cost: 16",
                .replacement = "ticks_per_cost: 16, features: { danger: -1 }",
                .onLineOf = "ticks_per_cost",
                .says = "danger cost" },
        Broken{ .from = "ticks_per_cost: 16",
                .replacement = "ticks_per_cost: 16, features: { danger: impassable }",
                .onLineOf = "ticks_per_cost",
                .says = "danger cost" },
        Broken{ .from = "ticks_per_cost: 16",
                .replacement = "ticks_per_cost: 16, features: { moat: 3 }",
                .onLineOf = "ticks_per_cost",
                .says = "moat" },
        Broken{ .from = "ticks_per_cost: 16",
                .replacement = "ticks_per_cost: 16, features: { road: 1001 }",
                .onLineOf = "ticks_per_cost",
                .says = "road cost" },
        Broken{ .from = "ticks_per_cost: 16",
                .replacement = "ticks_per_cost: 16, all_kinds: 1001",
                .onLineOf = "ticks_per_cost",
                .says = "all_kinds" },
        Broken{ .from = "sort: mountain_pass, ",
                .replacement = "sort: mountain_pass, draws: { traders: 1 }, ",
                .onLineOf = "key: wolf_pass",
                .says = "danger with draws" },
        Broken{ .from = "people: 100, ",
                .replacement = "people: 100, draws: { pilgrims: 1 }, ",
                .onLineOf = "key: capital",
                .says = "pilgrims" },
        Broken{ .from = "people: 100, ",
                .replacement = "people: 100, draws: { traders: 101 }, ",
                .onLineOf = "key: capital",
                .says = "traders draw" },
        Broken{ .from = "people: 100, ",
                .replacement = "people: 100, draws: { cultivators: 0, traders: 0 }, ",
                .onLineOf = "key: capital",
                .says = "draws no visitors" },
        Broken{ .from = "places:",
                .replacement = "travel: { stay_days: [5, 2] }\nplaces:",
                .onLineOf = "stay_days",
                .says = "stay_days" },
        Broken{ .from = "places:",
                .replacement = "travel: { stay_days: [0, 2] }\nplaces:",
                .onLineOf = "stay_days",
                .says = "stay_days" },
        Broken{ .from = "places:",
                .replacement = "travel: { share_per_mille: 1001 }\nplaces:",
                .onLineOf = "share_per_mille",
                .says = "share_per_mille" },
        Broken{ .from = "places:",
                .replacement = "travel: { reach_base: -1 }\nplaces:",
                .onLineOf = "reach_base",
                .says = "reach_base" },
        Broken{ .from = "ticks_per_cost: 16 }\n",
                .replacement = "ticks_per_cost: 16 }\n  - { key: on_foot, ticks_per_cost: 8 }\n",
                .onLineOf = "ticks_per_cost: 8",
                .says = "shares its key" },
    } };
    CheckRefused(CASES);
}

TEST_CASE("profiles read with their overrides resolved") {
    const std::string text = With(
        "  - { key: on_foot, ticks_per_cost: 16 }\n",
        "  - { key: on_foot, ticks_per_cost: 16 }\n"
        "  - { key: carriage, ticks_per_cost: 8, all_kinds: impassable, features: { road: 3 } }\n"
        "  - { key: boat, ticks_per_cost: 12, kinds: { deep_water: 5 }, features: { river: 6, ford: impassable } }\n"
    );
    const std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(text, Names());
    REQUIRE(map.has_value());
    REQUIRE(map->Profiles().size() == 3);
    const core::MovementProfile& walking = map->Profiles()[core::WALKING];
    CHECK(walking.key == "on_foot");
    CHECK(walking.ticksPerCost == 16);
    CHECK(walking.kindCosts == std::vector<u16>{ core::IMPASSABLE_COST, MEADOW_COST });
    CHECK(walking.features.road == 4);
    CHECK(walking.features.river == core::IMPASSABLE_COST);
    CHECK(walking.features.ford == 30);
    const core::MovementProfile& carriage = map->Profiles()[1];
    CHECK(carriage.ticksPerCost == 8);
    CHECK(carriage.kindCosts == std::vector<u16>{ core::IMPASSABLE_COST, core::IMPASSABLE_COST });
    CHECK(carriage.features.road == 3);
    CHECK(carriage.features.bridge == 4);
    const core::MovementProfile& boat = map->Profiles()[2];
    CHECK(boat.kindCosts == std::vector<u16>{ 5, MEADOW_COST });
    CHECK(boat.features.river == 6);
    CHECK(boat.features.ford == core::IMPASSABLE_COST);
}

TEST_CASE("danger reach and a profile's danger cost read") {
    const std::string text = With("ford_cost: 30", "ford_cost: 30\ndanger_reach: 3");
    const std::string priced = std::string{ text }.replace(
        text.find("ticks_per_cost: 16 }") + 20, 0, "\n  - { key: fearful, ticks_per_cost: 16, features: { danger: 25 } }"
    );
    const std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(priced, Names());
    REQUIRE(map.has_value());
    REQUIRE(map->Profiles().size() == 2);
    CHECK(map->Profiles()[1].features.danger == 25);
    CHECK(map->Profiles()[core::WALKING].features.danger == 0);
    // The danger at [4, 1] reaches [2, 2] (distance squared 5) and not the capital's [0, 2] (17).
    constexpr u32 NEAR_TILE = 12;
    constexpr u32 FAR_TILE = 10;
    CHECK((map->FlagsAt(NEAR_TILE) & std::to_underlying(core::TileFlag::Danger)) != 0);
    CHECK((map->FlagsAt(FAR_TILE) & std::to_underlying(core::TileFlag::Danger)) == 0);
    const std::expected<core::WorldMap, core::Error> plain = data::ParseWorldMap(std::string{ SMALL }, Names());
    REQUIRE(plain.has_value());
    CHECK(plain->Profiles()[core::WALKING].features.danger == 0);
    CHECK((plain->FlagsAt(NEAR_TILE) & std::to_underlying(core::TileFlag::Danger)) == 0);
}

// SMALL with a second profile, `wary`, and `bands` as the travel rules' bands.
std::string WithBands(std::string_view bands) {
    const std::string text = With("ticks_per_cost: 16 }\n", "ticks_per_cost: 16 }\n  - { key: wary, ticks_per_cost: 20 }\n");
    return std::string{ text }.replace(text.find("places:"), 7, std::format("travel: {{ bands: {} }}\nplaces:", bands));
}

TEST_CASE("bands read with their profiles resolved") {
    const std::expected<core::WorldMap, core::Error> map =
        data::ParseWorldMap(WithBands("[{ from_power: 0, profile: on_foot }, { from_power: 5.5, profile: wary }]"), Names());
    REQUIRE(map.has_value());
    REQUIRE(map->Travel().bands.size() == 2);
    CHECK(map->Travel().bands[0].fromPower == 0.0F);
    CHECK(map->Travel().bands[0].profile == 0);
    CHECK(map->Travel().bands[1].fromPower == 5.5F);
    CHECK(map->Travel().bands[1].profile == 1);
    const std::expected<core::WorldMap, core::Error> none = data::ParseWorldMap(WithBands("[]"), Names());
    REQUIRE(none.has_value());
    CHECK(none->Travel().bands.empty());
    const std::expected<core::WorldMap, core::Error> plain = data::ParseWorldMap(SMALL, Names());
    REQUIRE(plain.has_value());
    CHECK(plain->Travel().bands.empty());
}

TEST_CASE("an unknown key inside a band warns, with its line") {
    std::vector<std::string> warnings;
    const std::string text = WithBands("[{ from_power: 0, profile: on_foot, speed: 2 }]");
    const std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(text, Names(), &warnings);
    REQUIRE(map.has_value());
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].contains("speed"));
    CHECK(warnings[0].contains(std::format("line {}:", LineOf(text, "speed"))));
}

TEST_CASE("the reader refuses bad bands, naming the line") {
    const core::Strings strings = Names();
    const auto refused = [&](std::string_view bands, std::string_view says) {
        CAPTURE(bands);
        const std::string text = WithBands(bands);
        const std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(text, strings);
        REQUIRE_FALSE(map.has_value());
        CHECK(map.error().message.contains(says));
        CHECK(map.error().message.contains(std::format("line {}:", LineOf(text, "bands"))));
    };
    refused("[{ from_power: 1, profile: on_foot }]", "first band");
    refused(
        "[{ from_power: 0, profile: on_foot }, { from_power: 5, profile: wary }, { from_power: 5, profile: on_foot }]", "rise"
    );
    refused("[{ from_power: 0, profile: on_foot }, { from_power: 5, profile: skiff }]", "skiff");
    refused("[{ from_power: -1, profile: on_foot }]", "from_power");
    refused("{ from_power: 0, profile: on_foot }", "bands");
    refused("[{ from_power: 0 }]", "profile");
    std::string many = "[{ from_power: 0, profile: on_foot }";
    for (std::size_t band = 1; band <= core::MOST_PROFILES; ++band) {
        many += std::format(", {{ from_power: {}, profile: wary }}", band);
    }
    refused(many + "]", "too many bands");
}

TEST_CASE("draws and travel rules read") {
    const std::string text = With("people: 100, ", "people: 100, draws: { cultivators: 3, traders: 7 }, ");
    const std::string travelled = std::string{ text }.replace(
        text.find("places:"),
        7,
        "travel: { share_per_mille: 25, stay_days: [1, 9], reach_base: 1500, reach_per_power: 40, cultivator_power: 6 "
        "}\nplaces:"
    );
    const std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(travelled, Names());
    REQUIRE(map.has_value());
    CHECK(map->Places()[0].draws == std::array<u16, core::VISITOR_KINDS>{ { 3, 7 } });
    CHECK(map->Places()[1].draws == std::array<u16, core::VISITOR_KINDS>{ { 0, 0 } });
    CHECK(map->Travel().sharePerMille == 25);
    CHECK(map->Travel().stayMinDays == 1);
    CHECK(map->Travel().stayMaxDays == 9);
    CHECK(map->Travel().reachBase == 1500);
    CHECK(map->Travel().reachPerPower == 40);
    CHECK(map->Travel().cultivatorPower == doctest::Approx(TRAVEL_CULTIVATOR_POWER));
    const std::expected<core::WorldMap, core::Error> plain = data::ParseWorldMap(SMALL, Names());
    REQUIRE(plain.has_value());
    CHECK(plain->Places()[0].draws == std::array<u16, core::VISITOR_KINDS>{ { 1, 1 } });
    CHECK(plain->Travel().sharePerMille == 40);
    CHECK(plain->Travel().stayMaxDays == 5);
    CHECK(plain->Hash() != map->Hash());
}

TEST_CASE("a folded or quoted grid is refused, naming the grid's line, never misread") {
    const core::Strings strings = Names();
    const std::string folded = With("grid: |", "grid: >");
    const std::expected<core::WorldMap, core::Error> foldedMap = data::ParseWorldMap(folded, strings);
    REQUIRE_FALSE(foldedMap.has_value());
    const std::string foldedLine = std::format("line {}:", LineOf(folded, "grid:"));
    CHECK(foldedMap.error().message.contains(foldedLine));
    CHECK(foldedMap.error().message.contains("block"));
    const std::string quoted = With("grid: |\n  ~~~~~\n  ..~..\n  .....\n", "grid: \"~~~~~\\n..~..\\n.....\"\n");
    const std::expected<core::WorldMap, core::Error> quotedMap = data::ParseWorldMap(quoted, strings);
    REQUIRE_FALSE(quotedMap.has_value());
    const std::string quotedLine = std::format("line {}:", LineOf(quoted, "grid:"));
    CHECK(quotedMap.error().message.contains(quotedLine));
    CHECK(quotedMap.error().message.contains("block"));
}

TEST_CASE("an empty row is refused on its own line") {
    const std::string text = With("grid: |\n", "grid: |\n\n");
    const std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(text, Names());
    REQUIRE_FALSE(map.has_value());
    const std::string atLine = std::format("line {}:", LineOf(text, "grid:") + 1);
    CHECK(map.error().message.contains(atLine));
    CHECK(map.error().message.contains("empty"));
}

TEST_CASE("the reader refuses YAML that doesn't parse, and a map that isn't one") {
    const core::Strings strings = Names();
    const bool unparsed = data::ParseWorldMap("kinds: [", strings).has_value();
    const bool notAMap = data::ParseWorldMap("- a\n- b\n", strings).has_value();
    CHECK_FALSE(unparsed);
    CHECK_FALSE(notAMap);
}

TEST_CASE("an unknown key warns") {
    std::vector<std::string> warnings;
    const std::string text = With("road_cost: 4\n", "road_cost: 4\nweather: rain\n");
    const std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(text, Names(), &warnings);
    REQUIRE(map.has_value());
    REQUIRE(warnings.size() == 1);
    const std::string atLine = std::format("line {}:", LineOf(text, "weather"));
    CHECK(warnings[0].contains(atLine));
    CHECK(warnings[0].contains("weather"));
}

TEST_CASE("an unknown key inside a kind or a place warns, with its line") {
    std::vector<std::string> warnings;
    std::string kinds = With("people: 100,", "peopel: 100, people: 100,");
    kinds.insert(kinds.find("cost: 10"), "colour: green, ");
    const std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(kinds, Names(), &warnings);
    REQUIRE(map.has_value());
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0].contains("colour"));
    const std::string colourLine = std::format("line {}:", LineOf(kinds, "colour"));
    CHECK(warnings[0].contains(colourLine));
    CHECK(warnings[1].contains("peopel"));
    const std::string peopelLine = std::format("line {}:", LineOf(kinds, "peopel"));
    CHECK(warnings[1].contains(peopelLine));
}

// The shipped map, read as main reads it, has nothing to warn about. Its size and the port's tile are pinned to the
// constants in code by game_tests' shipped_world cases.
TEST_CASE("the shipped world map loads clean") {
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    REQUIRE(assets.has_value());
    const std::expected<core::Strings, core::Error> strings = data::LoadStrings(*assets / "data");
    REQUIRE(strings.has_value());
    // LoadWorldMap logs its warnings; the same text parsed here hands them back, and there are none.
    std::ifstream input{ *assets / "data" / "world.yaml", std::ios::binary };
    const std::string text{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    std::vector<std::string> warnings;
    const std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(text, *strings, &warnings);
    REQUIRE(map.has_value());
    CHECK(warnings.empty());
    CHECK(map->Places()[map->Start()].key == "capital");
    const bool missingLoads = data::LoadWorldMap(*assets / "no_such_folder", *strings).has_value();
    CHECK_FALSE(missingLoads);
}

TEST_CASE("the shipped world.yaml has on_foot as profile 0 at pace 16") {
    const std::shared_ptr<const core::WorldMap> map = Shipped();
    REQUIRE_FALSE(map->Profiles().empty());
    CHECK(map->Profiles()[core::WALKING].key == "on_foot");
    CHECK(map->Profiles()[core::WALKING].ticksPerCost == core::DEFAULT_TICKS_PER_COST);
    CHECK(map->Travel().stayMinDays <= map->Travel().stayMaxDays);
    CHECK(map->Travel().reachBase > 0);
    // capital, port, sect, oasis, marsh_town, isle
    struct Drawn {
        std::string_view key;
        std::array<u16, core::VISITOR_KINDS> draws;
    };
    constexpr std::array<Drawn, 6> CITIES{ {
        { .key = "capital", .draws = { { 3, 3 } } },
        { .key = "port", .draws = { { 1, 6 } } },
        { .key = "sect", .draws = { { 6, 1 } } },
        { .key = "oasis", .draws = { { 1, 5 } } },
        { .key = "marsh_town", .draws = { { 2, 2 } } },
        { .key = "isle", .draws = { { 2, 3 } } },
    } };
    for (std::size_t city = 0; city < CITIES.size(); ++city) {
        CAPTURE(city);
        CHECK(map->Places()[city].key == CITIES[city].key);
        CHECK(map->Places()[city].draws == CITIES[city].draws);
    }
}

// Mortals walk warily, round the danger sites where a way round exists; from the cultivators' power on, they walk past.
TEST_CASE("the shipped world.yaml walks mortals by on_foot_wary and cultivators by on_foot") {
    constexpr core::ProfileIndex WARY = 1;
    const std::shared_ptr<const core::WorldMap> map = Shipped();
    REQUIRE(map->Profiles().size() == 2);
    const core::MovementProfile& walking = map->Profiles()[core::WALKING];
    const core::MovementProfile& wary = map->Profiles()[WARY];
    CHECK(wary.key == "on_foot_wary");
    CHECK(wary.features.danger > 0);
    // Danger is all that sets the two apart.
    CHECK(wary.ticksPerCost == walking.ticksPerCost);
    CHECK(wary.kindCosts == walking.kindCosts);
    CHECK(wary.features.road == walking.features.road);
    CHECK(wary.features.river == walking.features.river);
    CHECK(wary.features.bridge == walking.features.bridge);
    CHECK(wary.features.ford == walking.features.ford);
    const std::vector<core::PowerBand>& bands = map->Travel().bands;
    REQUIRE(bands.size() == 2);
    CHECK(bands[0].fromPower == 0.0F);
    CHECK(bands[0].profile == WARY);
    CHECK(bands[1].fromPower == map->Travel().cultivatorPower);
    CHECK(bands[1].profile == core::WALKING);
    const u32 tiles = map->Columns() * map->Rows();
    u32 near = 0;
    for (u32 tile = 0; tile < tiles; ++tile) {
        near += (map->FlagsAt(tile) & std::to_underlying(core::TileFlag::Danger)) != 0 ? 1U : 0U;
    }
    CHECK(near > 0);
}

// Build refuses a city it can't reach, so this pins the rule on the shipped file; the danger sites, which Build leaves
// unchecked (the player will enter them), are reachable too.
TEST_CASE("every city on the shipped map is reachable from the capital, and every danger site too") {
    const std::shared_ptr<const core::WorldMap> map = Shipped();
    const u32 tiles = map->Columns() * map->Rows();
    std::vector<bool> reached(tiles, false);
    std::vector<u32> frontier{ map->TileIndex(map->Places()[map->Start()].tile) };
    reached.at(frontier.front()) = true;
    while (!frontier.empty()) {
        const core::TilePoint point = map->PointOf(frontier.back());
        const u32 from = frontier.back();
        frontier.pop_back();
        for (u32 row = point.row == 0 ? 0 : point.row - 1; row <= std::min(point.row + 1, map->Rows() - 1); ++row) {
            for (u32 column = point.column == 0 ? 0 : point.column - 1;
                 column <= std::min(point.column + 1, map->Columns() - 1);
                 ++column) {
                const u32 next = map->TileIndex({ .column = column, .row = row });
                if (!reached.at(next) && map->StepCost(from, next) != core::IMPASSABLE_COST) {
                    reached.at(next) = true;
                    frontier.push_back(next);
                }
            }
        }
    }
    const auto unreached =
        std::ranges::find_if(map->Places(), [&](const core::Place& place) { return !reached.at(map->TileIndex(place.tile)); });
    CHECK_MESSAGE(unreached == map->Places().end(), "a place no one can walk to from the capital");
}

// The anti-goal "cities without roads between them": walking reachability (above) holds over open ground alone, so this
// follows the road tiles themselves, edge to edge as roads are rasterised, from the capital to every city.
TEST_CASE("the shipped roads join every city to the capital") {
    const std::shared_ptr<const core::WorldMap> map = Shipped();
    constexpr u8 ROAD = std::to_underlying(core::TileFlag::Road);
    const auto onRoad = [&](u32 tile) { return (map->FlagsAt(tile) & ROAD) != 0; };
    const u32 capital = map->TileIndex(map->Places()[map->Start()].tile);
    REQUIRE(onRoad(capital));
    std::vector<bool> reached(static_cast<std::size_t>(map->Columns()) * map->Rows(), false);
    std::vector<u32> frontier{ capital };
    reached.at(capital) = true;
    while (!frontier.empty()) {
        const core::TilePoint point = map->PointOf(frontier.back());
        frontier.pop_back();
        const std::array<core::TilePoint, 4> sides{ { { .column = point.column + 1, .row = point.row },
                                                      { .column = point.column - 1, .row = point.row },
                                                      { .column = point.column, .row = point.row + 1 },
                                                      { .column = point.column, .row = point.row - 1 } } };
        for (const core::TilePoint side : sides) {
            // A step off the map's first column or row wraps to a huge value, which this bound also refuses.
            if (side.column >= map->Columns() || side.row >= map->Rows()) {
                continue;
            }
            const u32 next = map->TileIndex(side);
            if (!reached.at(next) && onRoad(next)) {
                reached.at(next) = true;
                frontier.push_back(next);
            }
        }
    }
    const auto cut = std::ranges::find_if(map->Places(), [&](const core::Place& place) {
        return place.kind == core::PlaceKind::City && !reached.at(map->TileIndex(place.tile));
    });
    CHECK_MESSAGE(cut == map->Places().end(), "a city no road from the capital reaches");
}

TEST_CASE("the shipped map has about six cities and twenty danger sites") {
    const std::shared_ptr<const core::WorldMap> map = Shipped();
    const auto cities = std::ranges::count(map->Places(), core::PlaceKind::City, &core::Place::kind);
    const auto dangers = std::ranges::count(map->Places(), core::PlaceKind::Danger, &core::Place::kind);
    CHECK(cities == SHIPPED_CITIES);
    CHECK(dangers >= LEAST_DANGERS);
    CHECK(dangers <= MOST_DANGERS);
}

} // namespace
