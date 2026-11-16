#pragma once

/// @file
/// Host saves built by hand for tests, laid out as game::Host::Save writes one, so a test can load a state no run reaches
/// cheaply (a world clock at a month's end, a refused mix of components). The format and versions must follow
/// game/host.cpp's, or every load of them is refused for the version alone.

#include "core/abilities.hpp"
#include "core/archive.hpp"
#include "core/frame.hpp"
#include "core/hit.hpp"
#include "core/journey.hpp"
#include "core/seal.hpp"
#include "core/stable_id.hpp"
#include "core/state.hpp"
#include "core/types.hpp"
#include "core/volley.hpp"
#include "game/combat.hpp"
#include "game/components.hpp"
#include "game/npcs.hpp"
#include "game/scenes.hpp"
#include "game/travel.hpp"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace test {

/// The host's save format (game/host.cpp's ARENA_FORMAT).
inline constexpr u32 HOST_FORMAT = 6;
/// The active domain's version in a host save (5: the NPCs' homes).
inline constexpr u32 ACTIVE_VERSION = 5;
/// The world domain's version in a host save (3: the travelling action, the journeys and the journals).
inline constexpr u32 WORLD_VERSION = 3;
/// The active domain's saved components, in the host's order.
using ActiveComponents = core::ComponentList<
    game::Body,
    game::Look,
    game::Orbit,
    game::Spin,
    game::Creature,
    game::Npc,
    game::Needs,
    game::Met,
    game::Wander,
    game::Health,
    game::Dying,
    game::Arriving,
    game::Caster,
    game::Dashing,
    game::Afflictions>;
/// The world domain's saved components, in the host's order.
using WorldComponents = core::ComponentList<game::Npc, game::Needs, game::Met, game::Group>;

/// The seed of the empty state WorldSaveAt() saves.
inline constexpr u64 EMPTY_SAVE_SEED = 7;

/// A volley as game::Combat saves one: the cast, then its targets.
struct SavedVolley {
    /// The cast.
    core::Volley volley{};
    /// Its targets.
    std::vector<core::VolleyTarget> targets;
};

/// The world clock, the month steps run and the world pause, as a save holds them.
struct SavedClock {
    /// The world clock (game::Host::WorldTicks()).
    u64 ticks = 0;
    /// The month steps run (game::Host::Months()).
    u64 months = 0;
    /// The world pause (game::Host::WorldPaused()).
    bool paused = false;
};

/// A save of `state` in `scene` with `player`: the format, the scene, the seed, the stable-ID counter, `clock`, the player and
/// the selection (none), then each domain after its version (`activeVersion`, `worldVersion`), then no relationships,
/// `travel`, the player's `route` if any, no respawns waiting, `volley` if any (the flying swords' hits of 20, none resolved
/// yet), `flash` if any and `seal` if any.
[[nodiscard]] inline std::vector<std::byte> ArenaSave(
    const core::State& state,
    core::StableId player = {},
    const std::optional<SavedVolley>& volley = std::nullopt,
    const std::optional<core::Flash>& flash = std::nullopt,
    const std::optional<core::Seal>& seal = std::nullopt,
    core::SceneKey scene = game::ARENA_SCENE,
    u32 worldVersion = WORLD_VERSION,
    u32 activeVersion = ACTIVE_VERSION,
    SavedClock clock = {},
    const game::Travel& travel = game::Travel{},
    const std::optional<core::Journey>& route = std::nullopt
) {
    constexpr u64 RELATIONSHIPS = 0;
    constexpr u64 RESPAWNS = 0;
    std::vector<std::byte> bytes;
    core::Writer writer{ bytes };
    writer(
        HOST_FORMAT,
        static_cast<u8>(std::to_underlying(scene)),
        state.Seed(),
        state.Ids().next,
        clock.ticks,
        clock.months,
        clock.paused,
        player,
        core::StableId{}
    );
    writer(activeVersion);
    core::SaveDomain(writer, state[game::ACTIVE], ActiveComponents{});
    writer(worldVersion);
    core::SaveDomain(writer, state[game::WORLD], WorldComponents{});
    writer(RELATIONSHIPS);
    travel.Save(writer);
    writer(route.has_value());
    if (route) {
        writer(*route);
    }
    writer(RESPAWNS);
    writer(static_cast<u64>(volley ? 1 : 0));
    if (volley) {
        constexpr i64 DAMAGE = 20;
        constexpr u32 RESOLVED = 0;
        writer(volley->volley, core::AbilityKind::FlyingSwords, core::HitKind::Damage, DAMAGE, core::Affliction{}, RESOLVED);
        writer(static_cast<u64>(volley->targets.size()));
        for (const core::VolleyTarget& target : volley->targets) {
            writer(target);
        }
    }
    writer(static_cast<u64>(flash ? 1 : 0));
    if (flash) {
        writer(*flash);
    }
    writer(static_cast<u64>(seal ? 1 : 0));
    if (seal) {
        writer(*seal);
    }
    return bytes;
}

/// A save of the world scene with nothing in it, at `clock`: a host with the world map loads it and runs its clock from
/// there without a month of ticks first.
[[nodiscard]] inline std::vector<std::byte> WorldSaveAt(SavedClock clock) {
    const core::State state{ game::DOMAINS, EMPTY_SAVE_SEED };
    return ArenaSave(
        state, {}, std::nullopt, std::nullopt, std::nullopt, game::WORLD_SCENE, WORLD_VERSION, ACTIVE_VERSION, clock
    );
}

} // namespace test
