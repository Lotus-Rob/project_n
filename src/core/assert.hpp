#pragma once

/// @file
/// @ingroup core
/// `PN_ASSERT` for broken invariants: a bug, never an expected failure (those return
/// `std::expected<T, core::Error>`, core/error.hpp).

#include <source_location>
#include <string_view>

namespace core::detail {

/// Logs the failed `condition` and `message` as Critical with their source location, flushes the
/// log and aborts, so a debugger stops here. Called by `PN_ASSERT`; use the macro instead.
[[noreturn]] void AssertFailed(std::string_view condition, std::string_view message, std::source_location where);

} // namespace core::detail

// NOLINTBEGIN(cppcoreguidelines-macro-usage): the condition's text (#condition) and its being
// compiled away in release builds need a macro.

#ifdef PN_ASSERTS
/// Checks an invariant: when `condition` is false, logs Critical with `message` and aborts. On in
/// debug and releasedbg builds (`PN_ASSERTS`); in release, `condition` isn't evaluated.
#define PN_ASSERT(condition, message)                                                                                          \
    ((condition) ? static_cast<void>(0) : ::core::detail::AssertFailed(#condition, (message), std::source_location::current()))
#else
/// Checks an invariant in debug and releasedbg builds; in release (here), `condition` isn't
/// evaluated: sizeof only keeps its names used.
#define PN_ASSERT(condition, message) static_cast<void>(sizeof(!(condition)))
#endif

// NOLINTEND(cppcoreguidelines-macro-usage)
