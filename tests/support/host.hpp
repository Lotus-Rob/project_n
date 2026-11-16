#pragma once

/// @file
/// Reading a game::Host's state in tests.

#include "core/ecs.hpp"
#include "core/stable_id.hpp"
#include "game/components.hpp"
#include "game/host.hpp"
#include "game/npcs.hpp"

namespace test {

/// `entity`'s body in `host`'s active domain; `entity` must have one.
[[nodiscard]] inline game::Body BodyOf(const game::Host& host, core::StableId entity) {
    const core::Domain& active = host.State()[game::ACTIVE];
    return active.Entities().get<game::Body>(active.Find(entity));
}

} // namespace test
