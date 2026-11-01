#pragma once

/// @file
/// @ingroup platform
/// Where the game may write.

#include "core/error.hpp"

#include <expected>
#include <filesystem>

namespace platform {

/// The per-user data folder (created if missing), writable even when the game is installed in a
/// read-only folder: logs and saves go here.
[[nodiscard]] std::expected<std::filesystem::path, core::Error> UserDataDirectory();

} // namespace platform
