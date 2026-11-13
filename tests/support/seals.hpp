#pragma once

/// @file
/// The seals for tests of an arena: those of a snapshot raised in play (the arena scene also publishes its showcase seals,
/// core::Seal::showcase, which are scene content), and the shipped looks (its target links data and platform, as
/// support/manifest.hpp's does).

#include "core/frame.hpp"
#include "core/seal.hpp"
#include "core/seals.hpp"
#include "data/seals.hpp"
#include "platform/paths.hpp"
#include "support/manifest.hpp"

#include <algorithm>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <iterator>
#include <utility>
#include <vector>

namespace test {

/// The seals of `snapshot` raised in play, in its order: all but its showcase seals.
[[nodiscard]] inline std::vector<core::Seal> RaisedSeals(const core::Snapshot& snapshot) {
    std::vector<core::Seal> raised;
    std::ranges::copy_if(snapshot.seals, std::back_inserter(raised), [](const core::Seal& seal) { return !seal.showcase; });
    return raised;
}

/// assets/data/seals.yaml, read against the shipped manifest (TestManifest()); a test binary that can't read it stops at
/// once, as TestManifest() does (data_tests' "the shipped seals table loads clean" says why).
[[nodiscard]] inline core::Seals ShippedSeals() {
    std::expected<core::Seals, core::Error> seals =
        platform::AssetsDirectory().and_then([](const std::filesystem::path& assets) {
            return data::LoadSeals(assets / "data", *TestManifest());
        });
    if (!seals) {
        std::abort(); // every test after would fail for the same reason
    }
    return std::move(*seals);
}

} // namespace test
