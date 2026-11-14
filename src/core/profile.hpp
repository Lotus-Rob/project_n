#pragma once

/// @file
/// @ingroup core
/// Profiling for game and engine code: the `PN_PROFILE_*` macros. They collect only in
/// `releasedbg` builds, for the Tracy viewer (`./dev profiler`), and do nothing everywhere else.
/// The only header that includes Tracy.

// NOLINTBEGIN(cppcoreguidelines-macro-usage): Tracy records each zone's source location in a
// static at the call site, which only a macro can place there.

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp> // IWYU pragma: export

/// Measures the enclosing scope as a zone named after its function: `PN_PROFILE_ZONE();` at the
/// top of every function that runs each frame.
// Not ZoneScoped: it ends in its own `;`, which leaves ours an empty statement (-Wextra-semi-stmt).
#define PN_PROFILE_ZONE() ZoneNamed(pn_profile_zone, true)
/// Ends a frame in the profiler's timeline: `PN_PROFILE_FRAME();` once per frame, after presenting.
#define PN_PROFILE_FRAME() FrameMark
/// Marks a moment in the profiler's timeline with `text` (a `std::string` or `std::string_view`), such as a scripted
/// run's next stage: rarely, never every frame.
#define PN_PROFILE_MESSAGE(text) TracyMessage((text).data(), (text).size())
#else
// Tracy's own disabled macros are empty, which leaves `PN_PROFILE_ZONE();` an empty statement
// (-Wextra-semi-stmt).
#define PN_PROFILE_ZONE() static_cast<void>(0)
#define PN_PROFILE_FRAME() static_cast<void>(0)
#define PN_PROFILE_MESSAGE(text) static_cast<void>(0)
#endif

// NOLINTEND(cppcoreguidelines-macro-usage)
