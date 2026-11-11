#pragma once

/// @file
/// @ingroup game
/// Combat (the phase 6 chunk 4 spec): creatures' health, the amount every hit deals, dying, and the respawns a death
/// queues. Health, damage and healing are i64, so long-term progression can grow amounts without bound while a fight
/// stays fast.

#include "core/abilities.hpp"
#include "core/archive.hpp"
#include "core/stable_id.hpp"
#include "core/state.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

#include <vector>

namespace game {

/// How many ticks a death's dissolve takes, from the hit that kills to the entity's removal: about half a second.
inline constexpr u64 DYING_TICKS = 30;
/// How many ticks after a fox's removal another takes its place.
inline constexpr u64 RESPAWN_TICKS = 180;
/// How many ticks a respawned fox takes to ease from the ground's edge into its orbit: about three seconds.
inline constexpr u32 ARRIVE_TICKS = 180;

/// A creature's health.
struct Health {
    /// What it has left: at 0 it is dying (Dying).
    i64 current = 0;
    /// Its most, which healing never passes.
    i64 max = 0;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.current, self.max);
    }

    /// Whether a value read from a save can be used: a positive most, and from nothing to it left.
    [[nodiscard]] bool Valid() const {
        return max > 0 && current >= 0 && current <= max;
    }
};

/// Marks a creature whose health ran out: it dissolves for DYING_TICKS, then is removed. It can't be hit, picked or
/// labelled meanwhile.
struct Dying {
    /// The simulation tick its health ran out.
    u64 startTick = 0;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.startTick);
    }
};

/// A respawned creature easing from where it appeared into its orbit: its foot goes from `from` to where its Orbit
/// puts it, eased in and out, over `ticks` ticks from `startTick`.
struct Arriving {
    /// Where it appeared.
    core::Vec2 from{};
    /// The simulation tick it appeared.
    u64 startTick = 0;
    /// How many ticks it takes.
    u32 ticks = 0;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.from, self.startTick, self.ticks);
    }

    /// Whether a value read from a save can be used: a finite start and at least one tick.
    [[nodiscard]] bool Valid() const {
        return core::IsFinite(from) && ticks > 0;
    }
};

/// The amount a hit of `base` deals or heals, before anything else: the one place every amount passes through, so
/// stats and levels plug in here later. Today it is `base`.
[[nodiscard]] i64 HitAmount(i64 base);

/// The amount a hit of `ability` with `base` deals or heals: HitAmount(`base`), with `ability`'s own part (none yet).
/// Every ability's hit and affliction tick calls it.
[[nodiscard]] i64 HitAmount(const core::AbilityData& ability, i64 base);

/// How far through its dissolve a creature dying since `dying.startTick` is at `tick`, from 0 (its death's tick) to 1
/// (its last tick drawn, the one before its removal).
[[nodiscard]] f32 DissolveAt(const Dying& dying, u64 tick);

/// The arena's combat state beyond its components: the respawns waiting.
class Combat {
public:
    /// Takes `amount` from `target`'s health (a negative amount heals, never past its most) at `tick`; health running
    /// out makes it Dying at `tick`. Does nothing to a target that isn't in `state`, has no Health, or is dying.
    static void Damage(core::State& state, core::StableId target, i64 amount, u64 tick);

    /// Advances combat in the active domain of `state` to `tick`, after the orbits have moved: eases each Arriving
    /// creature from where it appeared towards where its orbit put it (which it reaches, ending its arrival, at its last
    /// tick); removes each creature DYING_TICKS after its death, queueing a fox's respawn RESPAWN_TICKS later; and
    /// returns how many foxes are due to respawn at `tick`, which the caller spawns.
    [[nodiscard]] u32 Tick(core::State& state, u64 tick);

    /// The respawns waiting: the tick each is due, earliest first.
    [[nodiscard]] const std::vector<u64>& Respawns() const {
        return m_respawns;
    }

    /// Writes the respawns waiting into `archive` (a Writer or a Hasher): their count, then each one's tick.
    template<typename Archive>
    void Save(Archive& archive) const {
        archive(static_cast<u64>(m_respawns.size()));
        for (const u64 due : m_respawns) {
            archive(due);
        }
    }

    /// Reads what Save() wrote, failing `archive` and changing nothing on a count past what is left or ticks out of
    /// order.
    void Load(core::Reader& archive);

private:
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<u64> m_respawns{};
};

} // namespace game
