#pragma once

/// @file
/// @ingroup app
/// Why the main loop ends.

#include "core/types.hpp"

namespace app {

/// How the main loop ends, if it does.
enum class LoopEnd : u8 {
    /// It goes on.
    None,
    /// The game failed: the exit status says so.
    Failed,
    /// The player quit or `--quit-after` ran out.
    Quit,
};

/// A failure ends the loop as `Failed` whatever else asked to quit, so a failure in the turn that quit is not lost; else a
/// quit request or `--quit-after` ends it as `Quit`.
[[nodiscard]] constexpr LoopEnd EndOf(bool failed, bool quit, bool quitAfter) {
    if (failed) {
        return LoopEnd::Failed;
    }
    return quit || quitAfter ? LoopEnd::Quit : LoopEnd::None;
}

} // namespace app
