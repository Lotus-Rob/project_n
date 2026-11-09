#pragma once

/// @file
/// @ingroup core
/// The error of an expected failure, returned as `std::expected<T, core::Error>`.

#include <expected>
#include <string>

namespace core {

/// Why an operation failed, for the caller to log once at the level it chooses.
struct Error {
    /// What failed and why, in words a log line can carry: "no window: <SDL's reason>".
    std::string message;
};

/// The outcome of two steps that both ran: success when both succeeded, else the failure, or both
/// failures' messages when both failed, so the second cause isn't lost.
[[nodiscard]] inline std::expected<void, Error> Combine(
    const std::expected<void, Error>& first,
    const std::expected<void, Error>& second
) {
    if (!first && !second) {
        return std::unexpected{ Error{ first.error().message + "; " + second.error().message } };
    }
    return first ? second : first;
}

} // namespace core
