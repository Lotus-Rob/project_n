#pragma once

/// @file
/// The shipped world map for tests that need nothing above data and platform (world.hpp builds on it for the tests that
/// also link game). The test target copies assets/ next to it (the `assets` rule) and links data and platform.

#include "core/strings.hpp"
#include "core/world_map.hpp"
#include "data/strings.hpp"
#include "data/world_map.hpp"
#include "platform/paths.hpp"

#include <cstdlib>
#include <expected>
#include <filesystem>
#include <memory>
#include <utility>

namespace test {

/// assets/data/world.yaml, read against `strings`, for a test that has loaded them already. A test binary that can't read
/// the map stops at once, as TestManifest() does: every test that asks for it would fail for that same reason, and one
/// stop says it once.
[[nodiscard]] inline std::shared_ptr<const core::WorldMap> ShippedWorld(const core::Strings& strings) {
    std::expected<std::shared_ptr<const core::WorldMap>, core::Error> loaded =
        platform::AssetsDirectory().and_then([&](const std::filesystem::path& assets) {
            return data::LoadWorldMap(assets / "data", strings);
        });
    if (!loaded) {
        std::abort(); // every test after would fail for the same reason
    }
    return std::move(*loaded);
}

/// assets/data/world.yaml, read against the shipped strings (the overload above, with strings.yaml loaded here).
[[nodiscard]] inline std::shared_ptr<const core::WorldMap> ShippedWorld() {
    std::expected<core::Strings, core::Error> strings =
        platform::AssetsDirectory().and_then([](const std::filesystem::path& assets) {
            return data::LoadStrings(assets / "data");
        });
    if (!strings) {
        std::abort(); // as above
    }
    return ShippedWorld(*strings);
}

} // namespace test
