#pragma once

/// @file
/// @ingroup core
/// The error of an expected failure, returned as `std::expected<T, core::Error>`.

#include <string>

namespace core {

/// Why an operation failed, for the caller to log once at the level it chooses.
struct Error {
    /// What failed and why, in words a log line can carry: "no window: <SDL's reason>".
    std::string message;
};

} // namespace core
