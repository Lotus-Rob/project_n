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
places:
  - { key: capital, kind: city, tile: [0, 2], name: place_capital, people: 100, start: true }
  - { key: wolf_pass, kind: danger, sort: mountain_pass, tile: [4, 1], name: place_wolf_pass }
)yaml";

constexpr u32 COLUMNS = 5;
constexpr u32 ROWS = 3;
constexpr u32 PEOPLE = 100;
constexpr f32 TILE_SIZE = 256.0F;
constexpr u16 MEADOW_COST = 10;
constexpr u32 FORD_TILE = 8;
constexpr u32 SHIPPED_COLUMNS = 150;
constexpr u32 SHIPPED_ROWS = 85;
constexpr u32 SHIPPED_PEOPLE = 50'000;

core::Strings Names() {
    core::Strings strings;
    strings.Add("place_capital", { "Capital", "都城" });
    strings.Add("place_wolf_pass", { "Wolf Pass", "狼关" });
    return strings;
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
    CHECK(map.error().message.contains(std::format("line {}:", LineOf(text, "..\t.."))));
}

// A change to SMALL that the reader must refuse, and what the refusal must name.
struct Broken {
    std::string_view from;
    std::string_view replacement;
    std::string_view onLineOf; // the text of SMALL (as changed) whose line the refusal names
    std::string_view says;
};

TEST_CASE("the reader refuses, naming the line") {
    constexpr std::array<Broken, 17> CASES{ {
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
    } };
    const core::Strings strings = Names();
    for (const Broken& broken : CASES) {
        CAPTURE(broken.from);
        const std::string text = With(broken.from, broken.replacement);
        const std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(text, strings);
        REQUIRE_FALSE(map.has_value());
        CHECK(map.error().message.contains(broken.says));
        CHECK(map.error().message.contains(std::format("line {}:", LineOf(text, broken.onLineOf))));
    }
}

TEST_CASE("a folded or quoted grid is refused, naming the grid's line, never misread") {
    const core::Strings strings = Names();
    const std::string folded = With("grid: |", "grid: >");
    const std::expected<core::WorldMap, core::Error> foldedMap = data::ParseWorldMap(folded, strings);
    REQUIRE_FALSE(foldedMap.has_value());
    CHECK(foldedMap.error().message.contains(std::format("line {}:", LineOf(folded, "grid:"))));
    CHECK(foldedMap.error().message.contains("block"));
    const std::string quoted = With("grid: |\n  ~~~~~\n  ..~..\n  .....\n", "grid: \"~~~~~\\n..~..\\n.....\"\n");
    const std::expected<core::WorldMap, core::Error> quotedMap = data::ParseWorldMap(quoted, strings);
    REQUIRE_FALSE(quotedMap.has_value());
    CHECK(quotedMap.error().message.contains(std::format("line {}:", LineOf(quoted, "grid:"))));
    CHECK(quotedMap.error().message.contains("block"));
}

TEST_CASE("an empty row is refused on its own line") {
    const std::string text = With("grid: |\n", "grid: |\n\n");
    const std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(text, Names());
    REQUIRE_FALSE(map.has_value());
    CHECK(map.error().message.contains(std::format("line {}:", LineOf(text, "grid:") + 1)));
    CHECK(map.error().message.contains("empty"));
}

TEST_CASE("the reader refuses YAML that doesn't parse, and a map that isn't one") {
    const core::Strings strings = Names();
    CHECK_FALSE(data::ParseWorldMap("kinds: [", strings).has_value());
    CHECK_FALSE(data::ParseWorldMap("- a\n- b\n", strings).has_value());
}

TEST_CASE("an unknown key warns") {
    std::vector<std::string> warnings;
    const std::string text = With("road_cost: 4\n", "road_cost: 4\nweather: rain\n");
    const std::expected<core::WorldMap, core::Error> map = data::ParseWorldMap(text, Names(), &warnings);
    REQUIRE(map.has_value());
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].contains(std::format("line {}:", LineOf(text, "weather"))));
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
    CHECK(warnings[0].contains(std::format("line {}:", LineOf(kinds, "colour"))));
    CHECK(warnings[1].contains("peopel"));
    CHECK(warnings[1].contains(std::format("line {}:", LineOf(kinds, "peopel"))));
}

TEST_CASE("the shipped world map loads against the shipped strings") {
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    REQUIRE(assets.has_value());
    const std::expected<core::Strings, core::Error> strings = data::LoadStrings(*assets / "data");
    REQUIRE(strings.has_value());
    const std::expected<std::shared_ptr<const core::WorldMap>, core::Error> map =
        data::LoadWorldMap(*assets / "data", *strings);
    REQUIRE(map.has_value());
    CHECK((*map)->Columns() == SHIPPED_COLUMNS);
    CHECK((*map)->Rows() == SHIPPED_ROWS);
    REQUIRE((*map)->Places().size() == 1);
    CHECK((*map)->Places()[0].people == SHIPPED_PEOPLE);
    CHECK_FALSE(data::LoadWorldMap(*assets / "no_such_folder", *strings).has_value());
}

} // namespace
