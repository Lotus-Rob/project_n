#pragma once

/// @file
/// @ingroup core
/// Frame pacing for when vsync isn't available to pace frames.

#include "core/types.hpp"

namespace core {

/// How long to wait so that a frame that took `elapsedNs` lasts `targetNs` in all: the
/// difference, or zero once the frame has taken that long already.
[[nodiscard]] constexpr u64 FrameWait(u64 elapsedNs, u64 targetNs) {
    return elapsedNs < targetNs ? targetNs - elapsedNs : 0;
}

} // namespace core
