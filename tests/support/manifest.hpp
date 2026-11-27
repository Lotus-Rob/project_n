#pragma once

/// @file
/// The shipped atlas manifest for tests that build a host: their target
/// copies assets/ next to it (the `assets` rule) and links data and platform.

#include "core/manifest.hpp"
#include "data/manifest.hpp"
#include "platform/paths.hpp"

#include <cstdlib>
#include <expected>
#include <filesystem>
#include <memory>

namespace test {

/// assets/manifest.yaml, read once per test binary and kept until it exits, so a reference into it never dangles; every
/// caller shares it, and a test binary that can't read it stops at once.
[[nodiscard]] inline std::shared_ptr<const core::Manifest> TestManifest() {
    // Parsing the YAML is slow under AddressSanitizer and checks call this once per instance they compare; the manifest is
    // immutable, so sharing one leaks no state between tests.
#ifdef __clang__
    // Never destroyed, so every other static's destructor may still read it; GCC and MSVC would warn on the attribute.
    [[clang::no_destroy]]
#endif
    static const std::shared_ptr<const core::Manifest> shared = [] {
        std::expected<core::Manifest, core::Error> loaded =
            platform::AssetsDirectory().and_then([](const std::filesystem::path& assets) {
                return data::LoadManifest(assets / "manifest.yaml");
            });
        if (!loaded) {
            std::abort(); // every test after would fail for the same reason
        }
        return std::make_shared<const core::Manifest>(std::move(*loaded));
    }();
    return shared;
}

} // namespace test
