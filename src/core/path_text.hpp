#pragma once

/// @file
/// @ingroup core
/// Filesystem paths as UTF-8 text and back.

#include <filesystem>
#include <string>
#include <string_view>

namespace core {

/// The UTF-8 bytes of `path`, with its native separators. SDL and our logs speak UTF-8, while `path::string()` speaks
/// the ANSI code page on Windows and throws on a name outside it.
[[nodiscard]] std::string PathText(const std::filesystem::path& path);

/// The path whose `u8string()` is `utf8`. SDL's paths are UTF-8; a plain char path would be read in the Windows code
/// page.
[[nodiscard]] std::filesystem::path PathOf(std::string_view utf8);

} // namespace core
