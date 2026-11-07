#include "core/cxx23.hpp" // IWYU pragma: keep
#include "data/game_data.hpp"

#include <doctest/doctest.h>

namespace {

constexpr std::string_view CREATURES = R"(cultivator:
  sprite: cultivator
  walk_speed: 4.0
fox:
  sprite: fox
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
    const std::expected<core::GameData, core::Error> data = data::ParseGameData(creatures);
    REQUIRE_FALSE(data.has_value());
    return data.error();
}

// What CREATURES holds.
constexpr core::GameData PARSED{
    .cultivator = { .sprite = core::atlas::SpriteId::Cultivator, .walkSpeed = 4.0F },
    .fox = { .sprite = core::atlas::SpriteId::Fox,
             .walkSpeed = 0.5F,
             .orbitRadius = 300.0F,
             .orbitWobble = 40.0F,
             .orbitSpeed = -0.006F },
};

TEST_CASE("the creatures parse") {
    const std::expected<core::GameData, core::Error> data = data::ParseGameData(CREATURES);
    REQUIRE(data.has_value());
    CHECK(data->cultivator.sprite == PARSED.cultivator.sprite);
    CHECK(data->cultivator.walkSpeed == PARSED.cultivator.walkSpeed);
    CHECK(data->cultivator.orbitRadius == PARSED.cultivator.orbitRadius);
    CHECK(data->fox.sprite == PARSED.fox.sprite);
    CHECK(data->fox.walkSpeed == PARSED.fox.walkSpeed);
    CHECK(data->fox.orbitRadius == PARSED.fox.orbitRadius);
    CHECK(data->fox.orbitWobble == PARSED.fox.orbitWobble);
    CHECK(data->fox.orbitSpeed == PARSED.fox.orbitSpeed);
}

TEST_CASE("game data that differs hashes differently") {
    const std::expected<core::GameData, core::Error> data = data::ParseGameData(CREATURES);
    const std::expected<core::GameData, core::Error> again = data::ParseGameData(CREATURES);
    const std::expected<core::GameData, core::Error> faster = data::ParseGameData(With("walk_speed: 4.0", "walk_speed: 5.0"));
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
    constexpr std::array<Broken, 7> CASES{ {
        Broken{ .from = "walk_speed: 4.0", .replacement = "walk_speed: [4.0", .says = "line" },
        Broken{ .from = "  walk_speed: 4.0\n", .replacement = "", .says = "creatures line 2: cultivator has no walk_speed" },
        Broken{ .from = "walk_speed: 0.5", .replacement = "walk_speed: fast", .says = "line 6" },
        Broken{ .from = "walk_speed: 0.5",
                .replacement = "walk_speed: .inf",
                .says = "line 6: fox's walk_speed isn't a finite number" },
        Broken{ .from = "sprite: fox",
                .replacement = "sprite: dragon",
                .says = "creatures line 5: fox's sprite dragon isn't in the atlas" },
        Broken{ .from = "radius: 300.0, ", .replacement = "", .says = "fox's orbit has no radius" },
        Broken{ .from = "fox:", .replacement = "wolf:", .says = "no creature fox" },
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
