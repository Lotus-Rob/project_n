#pragma once

/// @file
/// Driving a game::Host and reading its state in tests.

#include "core/archive.hpp"
#include "core/ecs.hpp"
#include "core/frame.hpp"
#include "core/instances.hpp"
#include "core/jobs.hpp"
#include "core/stable_id.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"
#include "game/components.hpp"
#include "game/host.hpp"
#include "game/npcs.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace test {

/// `entity`'s body in `host`'s active domain; `entity` must have one.
[[nodiscard]] inline game::Body BodyOf(const game::Host& host, core::StableId entity) {
    const core::Domain& active = host.State()[game::ACTIVE];
    return active.Entities().get<game::Body>(active.Find(entity));
}

/// A fox's health in the built-in data (assets/data/creatures.yaml's): damage that kills one at full health.
inline constexpr i64 FOX_HEALTH = 1'200;

/// The counters of a tick that is tick `tick` on all three clocks.
[[nodiscard]] inline core::TickCounters At(u64 tick) {
    return { .wall = tick, .simulation = tick, .state = tick };
}

/// Ticks `host`, ignoring its events.
inline void Tick(game::Host& host, const core::TickCounters& counters, const core::TickInput& input) {
    std::vector<core::Event> events;
    host.Tick(counters, input, events);
}

/// The foxes in `host`'s active domain, dying ones included, in ID order.
[[nodiscard]] inline std::vector<core::StableId> Foxes(const game::Host& host) {
    std::vector<core::StableId> foxes;
    const core::Registry& entities = host.State()[game::ACTIVE].Entities();
    for (auto [entity, creature, stable] : entities.view<const game::Creature, const core::StableId>().each()) {
        if (creature.kind == game::CreatureKind::Fox) {
            foxes.push_back(stable);
        }
    }
    std::ranges::sort(foxes);
    return foxes;
}

/// `host`'s save, as bytes.
[[nodiscard]] inline std::vector<std::byte> Saved(game::Host& host) {
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    host.Save(writer);
    return bytes;
}

/// The instances `host` publishes on `jobs` with no view yet.
[[nodiscard]] inline std::vector<core::Instance> Published(game::Host& host, core::Jobs& jobs) {
    core::Snapshot snapshot;
    host.Publish(snapshot, jobs, std::nullopt);
    return snapshot.instances;
}

/// Whether two publishes drew the same instances, byte for byte.
[[nodiscard]] inline bool SameBytes(std::span<const core::Instance> lhs, std::span<const core::Instance> rhs) {
    return std::ranges::equal(std::as_bytes(lhs), std::as_bytes(rhs));
}

} // namespace test
