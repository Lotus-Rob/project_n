#pragma once

/// @file
/// @ingroup data
/// What the data module's readers share: reading a text file whole, and yaml-cpp's errors as ours.

#include "core/error.hpp"

#include <yaml-cpp/yaml.h>

#include <expected>
#include <filesystem>
#include <initializer_list>
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
