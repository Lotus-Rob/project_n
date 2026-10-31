#pragma once

/// @file
/// Logging for game and engine code: the `PN_LOG_*` macros, which take a format string and its
/// arguments like `std::format`. Formatting and writing happen on a background thread, so a call
/// costs little more than copying its arguments. The only header that includes Quill.

#include <quill/LogMacros.h> // IWYU pragma: export
#include <quill/Logger.h>    // IWYU pragma: export

namespace core::log {

/// Starts the background thread that writes log messages to the console. Call it once, first
/// thing in `main`. Messages still queued when the program exits are written before it ends.
void Start();

/// The logger every `PN_LOG_*` macro writes to; use the macros instead.
[[nodiscard]] quill::Logger* Root();

} // namespace core::log

// NOLINTBEGIN(cppcoreguidelines-macro-usage): each call site needs its own file, line and format
// string in a static, which only a macro can hand to Quill.

#ifndef __clang_analyzer__
/// Logs progress worth seeing in a normal run: `PN_LOG_INFO("loaded {} in {} ms", name, ms)`.
#define PN_LOG_INFO(...) QUILL_LOG_INFO(::core::log::Root(), __VA_ARGS__)
/// Logs something unexpected that the program recovers from.
#define PN_LOG_WARNING(...) QUILL_LOG_WARNING(::core::log::Root(), __VA_ARGS__)
/// Logs a failure: what failed and why.
#define PN_LOG_ERROR(...) QUILL_LOG_ERROR(::core::log::Root(), __VA_ARGS__)
#else
namespace core::log::detail {
template<typename... Args>
constexpr void Check(const char* /*format*/, const Args&... /*args*/) {
}
} // namespace core::log::detail
// clang-tidy only: its analyzer reports a false positive inside Quill's InlinedVector, which no
// suppression comment of ours can reach. The arguments are still analysed.
#define PN_LOG_INFO(...) ::core::log::detail::Check(__VA_ARGS__)
#define PN_LOG_WARNING(...) ::core::log::detail::Check(__VA_ARGS__)
#define PN_LOG_ERROR(...) ::core::log::detail::Check(__VA_ARGS__)
#endif

// NOLINTEND(cppcoreguidelines-macro-usage)
