#pragma once

/// @file
/// @ingroup core
/// Stable game IDs (design doc 0002 §7.1): how anything outside a domain or a tick names an
/// entity, without EnTT.

#include "core/types.hpp"

namespace core {

/// An entity's game ID, and a component every entity in the state carries: from a counter in the
/// state, never reused, saved with it. Saves, input, view models, relationships, commands, random
/// numbers and time slicing refer to entities by it; EnTT handles never leave a domain or a tick.
enum class StableId : u64 {
};

/// Hands out stable IDs; its count is saved with the state.
struct StableIdCounter {
    /// The ID the next Allocate() returns; 0 is never used, so it can mean "none".
    u64 next = 1;

    /// A new stable ID, never handed out before.
    [[nodiscard]] constexpr StableId Allocate() {
        return StableId{ next++ };
    }
};

} // namespace core
