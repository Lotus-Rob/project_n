#pragma once

/// @file
/// The shipped atlas manifest for tests that build an arena (design doc 0002.3.1 §6): their target
/// copies assets/ next to it (the `assets` rule) and links data and platform.

#include "core/manifest.hpp"
#include "data/manifest.hpp"
#include "platform/paths.hpp"

#include <cstdlib>
#include <expected>
#include <filesystem>
#include <memory>

namespace test {

/// assets/manifest.yaml; a test binary that can't read it stops at once.
[[nodiscard]] inline std::shared_ptr<const core::Manifest> TestManifest() {
    std::expected<core::Manifest, core::Error> loaded =
        platform::AssetsDirectory().and_then([](const std::filesystem::path& assets) {
            return data::LoadManifest(assets / "manifest.yaml");
        });
    if (!loaded) {
        std::abort(); // every test after would fail for the same reason
    }
    return std::make_shared<const core::Manifest>(std::move(*loaded));
}

} // namespace test
