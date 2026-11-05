#pragma once

/// @file
/// @ingroup core
/// Values read from the game's command line.

#include "core/error.hpp"
#include "core/types.hpp"

#include <expected>
#include <string_view>

namespace core {

/// `text`, a whole number of seconds above 0 such as a command line's "5", in nanoseconds; an
/// error for anything else: empty, 0, negative, fractional or with trailing characters.
[[nodiscard]] std::expected<u64, Error> SecondsToNs(std::string_view text);

} // namespace core
