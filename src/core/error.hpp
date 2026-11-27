#pragma once

/// @file
/// @ingroup core
/// The error of an expected failure, returned as `std::expected<T, core::Error>`.

#include <exception>
#include <expected>
#include <string>

namespace core {

/// Why an operation failed, for the caller to log once at the level it chooses.
struct Error {
    /// What failed and why, in words a log line can carry: "no window: <SDL's reason>".
    std::string message;
};

/// What a caught exception says in a log line or a dialog: its `what()` for a `std::exception`, "an exception of unknown
/// type" for anything else, and "no exception" for a null pointer. Called with `std::current_exception()` in a catch-all.
[[nodiscard]] inline std::string Describe(const std::exception_ptr& error) {
    if (error == nullptr) {
        return "no exception";
    }
    try {
        std::rethrow_exception(error);
    }
    catch (const std::exception& e) {
        return e.what();
    }
    catch (...) {
        return "an exception of unknown type";
    }
}

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
