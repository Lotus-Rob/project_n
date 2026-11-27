#pragma once

/// @file
/// @ingroup platform
/// What the game runs on.

#include <string>

namespace platform {

/// One line naming the machine for the log: "<OS name and version>, <n> logical CPUs, <m> MiB RAM".
/// The OS part falls back to the platform's name when the system won't say more.
[[nodiscard]] std::string SystemSummary();

} // namespace platform
