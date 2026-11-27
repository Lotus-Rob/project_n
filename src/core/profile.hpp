#pragma once

/// @file
/// @ingroup core
/// Profiling for game and engine code: the `PN_PROFILE_*` macros. They collect only in
/// `releasedbg` builds, for the Tracy viewer (`./dev profiler`), and do nothing everywhere else.
/// The only header that includes Tracy.
///
/// `./dev profile [--seconds N] [-- --stress]` records a run into `build/profiles/` (the game quits by itself with
/// `--quit-after N`, so no click into the viewer takes its focus and pauses it) and `./dev profiler <file>` opens it; or
/// live: `./dev run --mode releasedbg` and `./dev profiler`.
///
/// Anything else animating on the GPU skews a profile (a browser page with a moving canvas made the same build's frames
/// four to eight times slower): close animated pages, leave the game's window focused, and compare a build only with a
/// baseline taken the same way. A tiling compositor changes the window's size from `settings.yaml`'s, and a fullscreen
/// window may be paced at the monitor's rate despite mailbox (every zone ran three to five times slower than unthrottled):
/// record the size `niri msg windows` shows and compare zones only between runs paced alike.

// NOLINTBEGIN(cppcoreguidelines-macro-usage): Tracy records each zone's source location in a
// static at the call site, which only a macro can place there.

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp> // IWYU pragma: export

/// Measures the enclosing scope as a zone named after its function: `PN_PROFILE_ZONE();` at the
/// top of every function that runs each frame, except one called once per item, thousands of times a frame: its caller's
/// zone (or its job chunk's) measures it, because under a capture a zone's own cost is many times a small function's work.
// Not ZoneScoped: it ends in its own `;`, which leaves ours an empty statement (-Wextra-semi-stmt).
#define PN_PROFILE_ZONE() ZoneNamed(pn_profile_zone, true)
/// Ends a frame in the profiler's timeline: `PN_PROFILE_FRAME();` once per frame, after presenting.
#define PN_PROFILE_FRAME() FrameMark
/// Marks a moment in the profiler's timeline with `text` (a `std::string` or `std::string_view`), such as a scripted
/// run's next stage: rarely, never every frame. Never a bare literal (the macro calls `.data()`): name the text as a
/// `constexpr std::string_view`. The macros are empty outside `releasedbg`, so only that build (`check`'s, or `measure`)
/// fails on a wrong call.
#define PN_PROFILE_MESSAGE(text) TracyMessage((text).data(), (text).size())
#else
// Tracy's own disabled macros are empty, which leaves `PN_PROFILE_ZONE();` an empty statement
// (-Wextra-semi-stmt).
#define PN_PROFILE_ZONE() static_cast<void>(0)
#define PN_PROFILE_FRAME() static_cast<void>(0)
#define PN_PROFILE_MESSAGE(text) static_cast<void>(0)
#endif

// NOLINTEND(cppcoreguidelines-macro-usage)
