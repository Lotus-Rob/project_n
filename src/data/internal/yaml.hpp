#pragma once

/// @file
/// @ingroup data
/// What the data module's readers share: reading a text file whole, and yaml-cpp's errors as ours.

#include "core/error.hpp"
#include "core/types.hpp"

#include <yaml-cpp/yaml.h>

#include <expected>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace data::internal {

/// The text in `path`, or why it couldn't be read.
[[nodiscard]] std::expected<std::string, core::Error> ReadText(const std::filesystem::path& path);

/// Writes `text` to `path`, replacing what was there, or says why it couldn't.
[[nodiscard]] std::expected<void, core::Error> WriteText(const std::filesystem::path& path, std::string_view text);

/// `error` as one of ours: "`what` line N: yaml-cpp's reason" (the line when yaml-cpp knows it).
[[nodiscard]] core::Error ErrorOf(std::string_view what, const YAML::Exception& error);

/// The whole number from 0 to `limit` at `key` in `map` (0 when absent and not `required`), which `name` names in
/// errors: "`what` line N: ..." when it is missing, isn't a number, is negative, fractional or above `limit`.
[[nodiscard]] std::expected<i64, core::Error> WholeNumber(
    std::string_view what,
    const YAML::Node& map,
    std::string_view name,
    const char* key,
    bool required,
    i64 limit
);

/// The numbers a tuning value may take: from `low` (not itself when `lowOpen`) to `high`, which may be infinity.
struct Range {
    /// The least.
    f64 low = 0.0;
    /// Whether `low` itself is out.
    bool lowOpen = false;
    /// The most.
    f64 high = std::numeric_limits<f64>::infinity();
};

/// The finite number at `key` in `map` within `range` (`fallback` when absent), which `name` names in errors: "`what` line
/// N: ..." when it isn't a number or is out of range. The one reader of an optional tuning value.
[[nodiscard]] std::expected<f64, core::Error> TunedNumber(
    std::string_view what,
    const YAML::Node& map,
    std::string_view name,
    const char* key,
    f64 fallback,
    const Range& range
);

/// The finite number at `key` in `map` within `range`, as TunedNumber() reads one, but required: "`what` line N: `name` has
/// no `key`" when it is absent.
[[nodiscard]] std::expected<f64, core::Error> RequiredNumber(
    std::string_view what,
    const YAML::Node& map,
    std::string_view name,
    const char* key,
    const Range& range
);

/// The whole number from 0 to `limit` at `key` in `map` (`fallback` when absent), as WholeNumber() reads one.
[[nodiscard]] std::expected<i64, core::Error> TunedWhole(
    std::string_view what,
    const YAML::Node& map,
    std::string_view name,
    const char* key,
    i64 fallback,
    i64 limit
);

/// The seconds at `key` in `map` as simulation ticks (`fallback` when absent): rounded, at least one tick where above
/// zero, and at least `least` ticks, else refused as TunedNumber() refuses. A time of 0 where `least` is 0 is 0 ticks.
[[nodiscard]] std::expected<u32, core::Error> TunedTicks(
    std::string_view what,
    const YAML::Node& map,
    std::string_view name,
    const char* key,
    u32 fallback,
    u32 least
);

/// The seconds at `key` in `map` as simulation ticks, as TunedTicks() reads them, but required: "`what` line N: `name` has
/// no `key`" when they are absent.
[[nodiscard]] std::expected<u32, core::Error> RequiredTicks(
    std::string_view what,
    const YAML::Node& map,
    std::string_view name,
    const char* key,
    u32 least
);

/// Adds to `warnings` (when given) a line for each key of `map` not in `known`: "`what` line N:
/// `owner` has no `kind` K; it is ignored", or "there is no `kind` K" when `owner` is empty.
void NoteUnknownKeys(
    std::string_view what,
    const YAML::Node& map,
    std::string_view owner,
    std::string_view kind,
    std::initializer_list<std::string_view> known,
    std::vector<std::string>* warnings
);

} // namespace data::internal
