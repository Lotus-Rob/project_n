#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/game_data.hpp"
#include "core/world_map.hpp"
#include "game/host.hpp"
#include "game/npcs.hpp"
#include "game/scenes.hpp"
#include "game/scenes/world.hpp"
#include "support/manifest.hpp"
#include "support/world.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 SHIPPED_PEOPLE = 50'000;

// The shipped map's size stays in code until 5b2 (WORLD_COLUMNS, WORLD_ROWS, WORLD_TILE: the stand-in walkers' spread
// and gatherings are laid out on them), so the map must match it.
TEST_CASE("the shipped world map is the size the world scene's code assumes") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    CHECK(map->Columns() == game::WORLD_COLUMNS);
    CHECK(map->Rows() == game::WORLD_ROWS);
    CHECK(map->TileSize() == doctest::Approx(game::WORLD_TILE));
}

// `measure` counts the world's NPCs: the cities' shares are its load, as 5a's 50,000.
TEST_CASE("the shipped cities' shares sum to 50,000") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    u32 people = 0;
    u32 capital = 0;
    u32 largest = 0;
    for (const core::Place& place : map->Places()) {
        people += place.kind == core::PlaceKind::City ? place.people : 0;
        capital = place.key == "capital" ? place.people : capital;
        largest = std::max(largest, place.people);
    }
    CHECK(people == SHIPPED_PEOPLE);
    CHECK(capital == largest);
}

// The measurement path (app/camera_path) flies over WORLD_BUSIEST, where the busiest stand-in gathering sits round the
// port: moving the port off it would measure an empty sea.
TEST_CASE("WORLD_BUSIEST lies in the port's tile") {
    const std::shared_ptr<const core::WorldMap> map = test::ShippedWorld();
    const auto port = std::ranges::find(map->Places(), std::string_view{ "port" }, &core::Place::key);
    REQUIRE(port != map->Places().end());
    CHECK(port->kind == core::PlaceKind::City);
    CHECK(port->tile.column == static_cast<u32>(game::WORLD_BUSIEST.x / map->TileSize()));
    CHECK(port->tile.row == static_cast<u32>(game::WORLD_BUSIEST.y / map->TileSize()));
}

// ShippedData() is what every host that builds the world scene takes: the built-in data with the shipped map.
TEST_CASE("the shipped data carries the shipped world map") {
    const core::GameData data = test::ShippedData();
    REQUIRE(data.world != nullptr);
    CHECK(data.world->Places()[data.world->Start()].key == "capital");
    const u64 builtIn = game::BuiltInData().Hash(); // named first: a temporary inside CHECK trips MSVC's C4866
    CHECK(data.Hash() != builtIn);
}

// 80% mortals (below 5, the cultivator threshold), 18% cultivators (5 to 50), 2% strong (50 and up), within 2 points.
TEST_CASE("the shipped world's people are mostly mortals, some cultivators, a rare few strong") {
    constexpr f32 CULTIVATOR = 5.0F;
    constexpr f32 STRONG = 50.0F;
    constexpr f64 TOLERANCE = 2.0;
    constexpr f64 MORTAL_SHARE = 80.0;
    constexpr f64 CULTIVATOR_SHARE = 18.0;
    constexpr f64 STRONG_SHARE = 2.0;
    game::Host const host(test::TestManifest(), game::WORLD_SCENE, test::ShippedData());
    u32 mortals = 0;
    u32 cultivators = 0;
    u32 strong = 0;
    for (const auto [entity, person] : host.State()[game::WORLD].Entities().view<const game::Npc>().each()) {
        mortals += person.power < CULTIVATOR ? 1U : 0U;
        cultivators += person.power >= CULTIVATOR && person.power < STRONG ? 1U : 0U;
        strong += person.power >= STRONG ? 1U : 0U;
    }
    const auto share = [](u32 count) { return 100.0 * count / SHIPPED_PEOPLE; };
    CHECK(mortals + cultivators + strong == SHIPPED_PEOPLE);
    CHECK(std::abs(share(mortals) - MORTAL_SHARE) <= TOLERANCE);
    CHECK(std::abs(share(cultivators) - CULTIVATOR_SHARE) <= TOLERANCE);
    CHECK(std::abs(share(strong) - STRONG_SHARE) <= TOLERANCE);
}

} // namespace
