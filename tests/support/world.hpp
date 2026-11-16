#pragma once

/// @file
/// The shipped world map for tests: assets/data/world.yaml read against the shipped strings, as main reads it, the shipped
/// names, and the game data a world scene runs on in the game (game::BuiltInData() with that map and those names), since
/// game::BuiltInData() carries neither. Their
/// target copies assets/ next to it (the `assets` rule) and links data and platform, and game for ShippedData().

#include "core/game_data.hpp"
#include "core/manifest.hpp"
#include "core/names.hpp"
#include "core/strings.hpp"
#include "core/world_look.hpp"
#include "core/world_map.hpp"
#include "data/names.hpp"
#include "data/strings.hpp"
#include "data/world_look.hpp"
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

/// assets/data/world_look.yaml, read against `manifest` and `map` (ShippedWorld()); a test binary that can't read it stops at
/// once, as ShippedWorld() does.
[[nodiscard]] inline core::WorldLook ShippedWorldLook(const core::Manifest& manifest, const core::WorldMap& map) {
    std::expected<core::WorldLook, core::Error> look =
        platform::AssetsDirectory().and_then([&](const std::filesystem::path& assets) {
            return data::LoadWorldLook(assets / "data", manifest, map);
        });
    if (!look) {
        std::abort(); // every test after would fail for the same reason
    }
    return std::move(*look);
}

/// assets/data/names.yaml; a test binary that can't read it stops at once, as ShippedWorld() does.
[[nodiscard]] inline std::shared_ptr<const core::Names> ShippedNames() {
    std::expected<core::Names, core::Error> names =
        platform::AssetsDirectory().and_then([](const std::filesystem::path& assets) {
            return data::LoadNames(assets / "data" / "names.yaml");
        });
    if (!names) {
        std::abort(); // every test after would fail for the same reason
    }
    return std::make_shared<const core::Names>(std::move(*names));
}

/// game::BuiltInData() with the shipped world map and names: the data every host that builds or cycles into the world
/// scene needs, its travellers named as in the game.
[[nodiscard]] inline core::GameData ShippedData() {
    core::GameData data = game::BuiltInData();
    data.world = ShippedWorld();
    data.names = ShippedNames();
    return data;
}

} // namespace test
