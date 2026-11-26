#pragma once

/// @file
/// Driving a game::Host and reading its state in tests.

#include "core/archive.hpp"
#include "core/ecs.hpp"
#include "core/frame.hpp"
#include "core/game_data.hpp"
#include "core/instances.hpp"
#include "core/jobs.hpp"
#include "core/rect.hpp"
#include "core/stable_id.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
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

/// Ticks `host` once as tick `tick` on all three clocks, holding `input`, ignoring its events.
inline void Tick(game::Host& host, u64 tick, const core::TickInput& input = {}) {
    Tick(host, At(tick), input);
}

/// Where `entity`'s foot is now in `host`'s active domain; `entity` must have a body.
[[nodiscard]] inline core::Vec2 FootOf(const game::Host& host, core::StableId entity) {
    return BodyOf(host, entity).current;
}

/// Lets a month pass for `host` at once, as M asks for it: a month step at tick `tick` on all three clocks.
inline void StepMonth(game::Host& host, u64 tick) {
    core::Jobs jobs{ 1 };
    host.AdvanceCalendar(At(tick), jobs, {});
}

/// Lets a month pass for `host`, as the logic does on the frozen tick after a transition: a month step at state tick
/// `state` with the simulation clock at 0.
inline void StepMonthFrozen(game::Host& host, u64 state) {
    core::Jobs jobs{ 1 };
    host.AdvanceCalendar({ .wall = state, .simulation = 0, .state = state }, jobs, {});
}

/// Game data that walks the cultivator to 100 units from the crystal in one tick and circles the foxes 300 units round
/// it, which puts some of them in the aura's reach and some out.
[[nodiscard]] inline core::GameData NearTheCrystal() {
    constexpr f32 WALK_SPEED = 440.0F;
    constexpr f32 FOX_ORBIT = 300.0F;
    core::GameData data = game::BuiltInData();
    data.cultivator.walkSpeed = WALK_SPEED;
    data.fox.orbitRadius = FOX_ORBIT;
    data.fox.orbitWobble = 0.0F;
    return data;
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

/// The foxes in `host`'s active domain that are not dying, in ID order.
[[nodiscard]] inline std::vector<core::StableId> LivingFoxes(const game::Host& host) {
    std::vector<core::StableId> foxes;
    const core::Registry& entities = host.State()[game::ACTIVE].Entities();
    for (auto [entity, creature, stable] : entities.view<const game::Creature, const core::StableId>().each()) {
        if (creature.kind == game::CreatureKind::Fox && !entities.all_of<game::Dying>(entity)) {
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

/// What `host` publishes culled to `view`, or with none sent yet when empty, on one worker.
[[nodiscard]] inline core::Snapshot PublishedSnapshot(game::Host& host, const std::optional<core::Rect>& view = std::nullopt) {
    core::Jobs jobs{ 1 };
    core::Snapshot snapshot;
    host.Publish(snapshot, jobs, view);
    return snapshot;
}

/// What `host` publishes at simulation tick `tick` with no view sent yet, on one worker.
[[nodiscard]] inline core::Snapshot PublishedAt(game::Host& host, u64 tick) {
    core::Jobs jobs{ 1 };
    core::Snapshot snapshot;
    snapshot.simulationTick = tick;
    host.Publish(snapshot, jobs, std::nullopt);
    return snapshot;
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
