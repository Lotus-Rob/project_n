#pragma once

/// @file
/// @ingroup data
/// What the data module's readers share: reading a text file whole, yaml-cpp's errors as ours, tuning numbers, names from a
/// closed list and colours from the palette.

#include "core/error.hpp"
#include "core/glow.hpp"
#include "core/types.hpp"
#include "core/ui_colours.hpp"

#include <yaml-cpp/yaml.h>

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <limits>
#include <optional>
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

/// A fraction: from 0 to 1, both included.
inline constexpr Range FRACTION{ .low = 0.0, .lowOpen = false, .high = 1.0 };

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

/// The refusal "`what` line N: `why`", N the line of `node`.
[[nodiscard]] core::Error RefusalAt(std::string_view what, const YAML::Node& node, std::string_view why);

/// The names of core::GlowCurve's values in the data files, in its order.
inline constexpr std::array<std::string_view, 5> CURVE_NAMES{ { "linear", "ease_in", "ease_out", "smooth", "sine" } };

/// Sets `field` to the entry of `names` that `key` in `map` spells, its index as an `Enum` (leaving it when absent), or
/// refuses: "`what` line N: `owner`'s `key` "text" isn't known".
template<typename Enum, std::size_t Count>
[[nodiscard]] std::expected<void, core::Error> ReadNamed(
    std::string_view what,
    const YAML::Node& map,
    std::string_view owner,
    const char* key,
    const std::array<std::string_view, Count>& names,
    Enum& field
) {
    const YAML::Node node = map[key];
    if (!node) {
        return {};
    }
    const std::string text = node.IsScalar() ? node.Scalar() : std::string{};
    for (std::size_t index = 0; index < Count; ++index) {
        if (names.at(index) == text) {
            field = static_cast<Enum>(index);
            return {};
        }
    }
    return std::unexpected{ RefusalAt(what, node, std::format("{}'s {} \"{}\" isn't known", owner, key, text)) };
}

/// The palette colour `<area>.<key>` names (core::UI_COLOUR_INFO), if it names one.
[[nodiscard]] std::optional<core::UiColour> PaletteColour(std::string_view name);

/// The palette colour `node` names as `<area>.<key>`, or "`what` line N: `owner` has the colour "name", which isn't a
/// palette colour (area.key)".
[[nodiscard]] std::expected<core::UiColour, core::Error> ReadPalette(
    std::string_view what,
    const YAML::Node& node,
    std::string_view owner
);

/// The glow colour `node` names: `element` (the element colour of what draws it) or a palette colour, which ReadPalette()
/// reads and refuses.
[[nodiscard]] std::expected<core::GlowColour, core::Error> ReadGlowColour(
    std::string_view what,
    const YAML::Node& node,
    std::string_view owner
);

} // namespace data::internal
