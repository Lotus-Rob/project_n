#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/game_data.hpp"
#include "core/world_map.hpp"
#include "game/host.hpp"
#include "game/scenes/world.hpp"
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

} // namespace
