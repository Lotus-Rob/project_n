#pragma once

/// @file
/// @ingroup core
/// Logging for game and engine code: the `PN_LOG_*` macros, which take a format string and its
/// arguments like `std::format`. Formatting and writing happen on a background thread, so a call
/// costs little more than copying its arguments. The only header that includes Quill.

#include <quill/LogMacros.h> // IWYU pragma: export
#include <quill/Logger.h>    // IWYU pragma: export

#include <filesystem>

namespace core::log {

/// Starts the background thread that writes log messages: Info and above to the console, and
/// everything from Debug up to `directory/project_n.log`. Each start begins a new file there and
/// keeps the previous 9 (`project_n.1.log`, the newest, to `project_n.9.log`). With an empty
/// `directory` it logs to the console only (the caller says why); with one that can't be used, it
/// logs to the console only and warns why. Call it first thing in `main`; later calls change
/// nothing. Messages still queued when the program exits are written before it ends.
void Start(const std::filesystem::path& directory);

/// The logger every `PN_LOG_*` macro writes to; use the macros instead. Before Start(), it writes
/// to the console only.
[[nodiscard]] quill::Logger* Root();

} // namespace core::log

// NOLINTBEGIN(cppcoreguidelines-macro-usage): each call site needs its own file, line and format
// string in a static, which only a macro can hand to Quill.

/// Logs detail for diagnosing a problem: decisions, state changes, values. Log file only.
#define PN_LOG_DEBUG(...) QUILL_LOG_DEBUG(::core::log::Root(), __VA_ARGS__)
/// Logs progress worth seeing in a normal run: `PN_LOG_INFO("loaded {} in {} ms", name, ms)`.
#define PN_LOG_INFO(...) QUILL_LOG_INFO(::core::log::Root(), __VA_ARGS__)
/// Logs a significant normal event, such as the program starting or stopping.
#define PN_LOG_NOTICE(...) QUILL_LOG_NOTICE(::core::log::Root(), __VA_ARGS__)
/// Logs something unexpected that the program recovers from.
#define PN_LOG_WARNING(...) QUILL_LOG_WARNING(::core::log::Root(), __VA_ARGS__)
/// Logs a failure: what failed and why.
#define PN_LOG_ERROR(...) QUILL_LOG_ERROR(::core::log::Root(), __VA_ARGS__)
/// Logs a failure the program can't continue after.
#define PN_LOG_CRITICAL(...) QUILL_LOG_CRITICAL(::core::log::Root(), __VA_ARGS__)

// NOLINTEND(cppcoreguidelines-macro-usage)
