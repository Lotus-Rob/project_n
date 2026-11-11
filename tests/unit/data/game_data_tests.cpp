#include "core/cxx23.hpp" // IWYU pragma: keep
#include "data/game_data.hpp"

#include <doctest/doctest.h>

namespace {

constexpr std::string_view ABILITIES = "qi: { max: 100, start: 50 }\n";

constexpr std::string_view CREATURES = R"(cultivator:
  sprite: cultivator
  health: 1000
  walk_speed: 4.0
fox:
  sprite: fox
  health: 1200
  walk_speed: 0.5
  orbit: { radius: 300.0, wobble: 40.0, speed: -0.006 }
)";

// CREATURES with `from` replaced by `to`.
std::string With(std::string_view from, std::string_view replacement) {
    std::string text{ CREATURES };
    const std::size_t where = text.find(from);
    REQUIRE(where != std::string::npos);
    return text.replace(where, from.size(), replacement);
}

core::Error ErrorFor(const std::string& creatures) {
    const std::expected<core::GameData, core::Error> data = data::ParseGameData(creatures, ABILITIES);
    REQUIRE_FALSE(data.has_value());
    return data.error();
}

TEST_CASE("an unknown key in the creatures is reported with its line, and the rest still parse") {
    std::vector<std::string> warnings;
    const std::expected<core::GameData, core::Error> data =
        data::ParseGameData(With("walk_speed: 0.5", "walk_speed: 0.5\n  walk_sped: 9.0"), ABILITIES, &warnings);
    REQUIRE(data.has_value());
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "creatures line 9: fox has no key walk_sped; it is ignored");
}

TEST_CASE("creatures that use only known keys report nothing") {
    std::vector<std::string> warnings;
    const std::expected<core::GameData, core::Error> data = data::ParseGameData(CREATURES, ABILITIES, &warnings);
    REQUIRE(data.has_value());
    CHECK(warnings.empty());
}

// What CREATURES holds.
constexpr core::GameData PARSED{
    .cultivator = { .sprite = core::atlas::SpriteId::Cultivator, .health = 1000, .walkSpeed = 4.0F },
    .fox = { .sprite = core::atlas::SpriteId::Fox,
             .health = 1200,
             .walkSpeed = 0.5F,
             .orbitRadius = 300.0F,
             .orbitWobble = 40.0F,
             .orbitSpeed = -0.006F },
};

TEST_CASE("the creatures parse") {
    const std::expected<core::GameData, core::Error> data = data::ParseGameData(CREATURES, ABILITIES);
    REQUIRE(data.has_value());
    CHECK(data->cultivator.sprite == PARSED.cultivator.sprite);
    CHECK(data->cultivator.walkSpeed == PARSED.cultivator.walkSpeed);
    CHECK(data->cultivator.orbitRadius == PARSED.cultivator.orbitRadius);
    CHECK(data->fox.sprite == PARSED.fox.sprite);
    CHECK(data->fox.walkSpeed == PARSED.fox.walkSpeed);
    CHECK(data->fox.orbitRadius == PARSED.fox.orbitRadius);
    CHECK(data->fox.orbitWobble == PARSED.fox.orbitWobble);
    CHECK(data->fox.orbitSpeed == PARSED.fox.orbitSpeed);
    CHECK_FALSE(data->fox.orbitsPlayer); // around the start unless it says otherwise
}

TEST_CASE("an orbit goes around the player when it says so") {
    const std::expected<core::GameData, core::Error> data =
        data::ParseGameData(With("speed: -0.006 }", "speed: -0.006, around: player }"), ABILITIES);
    REQUIRE(data.has_value());
    CHECK(data->fox.orbitsPlayer);
}

TEST_CASE("game data that differs hashes differently") {
    const std::expected<core::GameData, core::Error> data = data::ParseGameData(CREATURES, ABILITIES);
    const std::expected<core::GameData, core::Error> again = data::ParseGameData(CREATURES, ABILITIES);
    const std::expected<core::GameData, core::Error> faster =
        data::ParseGameData(With("walk_speed: 4.0", "walk_speed: 5.0"), ABILITIES);
    REQUIRE(data.has_value());
    REQUIRE(again.has_value());
    REQUIRE(faster.has_value());
    CHECK(data->Hash() == again->Hash());
    CHECK(data->Hash() != faster->Hash());
}

// A change to CREATURES that breaks it, and what the error must say.
struct Broken {
    std::string_view from;
    std::string_view replacement;
    std::string_view says;
};

TEST_CASE("malformed creatures are errors naming the line") {
    constexpr std::array<Broken, 9> CASES{ {
        Broken{ .from = "walk_speed: 4.0", .replacement = "walk_speed: [4.0", .says = "line" },
        Broken{ .from = "  walk_speed: 4.0\n", .replacement = "", .says = "creatures line 2: cultivator has no walk_speed" },
        Broken{ .from = "walk_speed: 0.5", .replacement = "walk_speed: fast", .says = "line 8" },
        Broken{ .from = "walk_speed: 0.5",
                .replacement = "walk_speed: .inf",
                .says = "line 8: fox's walk_speed isn't a finite number" },
        Broken{ .from = "sprite: fox",
                .replacement = "sprite: dragon",
                .says = "creatures line 6: fox's sprite dragon isn't in the atlas" },
        Broken{ .from = "radius: 300.0, ", .replacement = "", .says = "fox's orbit has no radius" },
        Broken{ .from = "health: 1200", .replacement = "health: 0", .says = "creatures line 7: fox's health is below 1" },
        Broken{ .from = "fox:", .replacement = "wolf:", .says = "no creature fox" },
        Broken{ .from = "speed: -0.006 }",
                .replacement = "speed: -0.006, around: moon }",
                .says = "line 9: fox's orbit goes around moon, not the player or the start" },
    } };
    for (const Broken& broken : CASES) {
        CAPTURE(broken.says);
        const core::Error error = ErrorFor(With(broken.from, broken.replacement));
        CHECK(error.message.contains(broken.says));
    }
    const core::Error notAMap = ErrorFor("- cultivator\n");
    CHECK(notAMap.message.contains("not a map"));
}

TEST_CASE("a missing data folder is an error") {
    const std::expected<core::GameData, core::Error> data =
        data::LoadGameData(std::filesystem::temp_directory_path() / "project_n_no_such_data_folder");
    CHECK_FALSE(data.has_value());
}

} // namespace
