#pragma once

/// @file
/// @ingroup data
/// What the data module's readers share: reading a text file whole, and yaml-cpp's errors as ours.

#include "core/error.hpp"

#include <yaml-cpp/yaml.h>

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace data::internal {

/// The text in `path`, or why it couldn't be read.
[[nodiscard]] std::expected<std::string, core::Error> ReadText(const std::filesystem::path& path);

/// Writes `text` to `path`, replacing what was there, or says why it couldn't.
[[nodiscard]] std::expected<void, core::Error> WriteText(const std::filesystem::path& path, std::string_view text);

/// `error` as one of ours: "`what` line N: yaml-cpp's reason" (the line when yaml-cpp knows it).
[[nodiscard]] core::Error ErrorOf(std::string_view what, const YAML::Exception& error);

} // namespace data::internal
