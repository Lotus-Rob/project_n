#pragma once

/// @file
/// The shipped world map for tests: assets/data/world.yaml read against the shipped strings, as main reads it, and the
/// game data a world scene needs (game::BuiltInData() with that map), since game::BuiltInData() carries no map. Their
/// target copies assets/ next to it (the `assets` rule) and links data and platform, and game for ShippedData().

#include "core/game_data.hpp"
#include "core/strings.hpp"
#include "core/world_map.hpp"
#include "data/strings.hpp"
#include "data/world_map.hpp"
#include "game/host.hpp"
#include "platform/paths.hpp"

#include <cstdlib>
#include <expected>
#include <filesystem>
#include <memory>

namespace test {

/// assets/data/world.yaml, read against the shipped strings. A test binary that can't read either stops at once, as
/// TestManifest() does: every test that asks for the map would fail for that same reason, and one stop says it once.
[[nodiscard]] inline std::shared_ptr<const core::WorldMap> ShippedWorld() {
    std::expected<std::shared_ptr<const core::WorldMap>, core::Error> loaded =
        platform::AssetsDirectory().and_then([](const std::filesystem::path& assets) {
            return data::LoadStrings(assets / "data").and_then([&](const core::Strings& strings) {
                return data::LoadWorldMap(assets / "data", strings);
            });
        });
    if (!loaded) {
        std::abort(); // every test after would fail for the same reason
    }
    return std::move(*loaded);
}

/// game::BuiltInData() with the shipped world map: the data every host that builds or cycles into the world scene needs.
[[nodiscard]] inline core::GameData ShippedData() {
    core::GameData data = game::BuiltInData();
    data.world = ShippedWorld();
    return data;
}

} // namespace test
