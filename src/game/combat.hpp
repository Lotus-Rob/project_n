#pragma once

/// @file
/// @ingroup game
/// Combat (the phase 6 chunk 4 spec): creatures' health, the amount every hit deals, the volleys in flight (the flying
/// swords and the fire bolt), the poison lance's strikes and their flashes, the burns and poisons hits leave, dying, and
/// the respawns a death queues. Health, damage and healing are i64, so long-term progression can grow amounts without
/// bound while a fight stays fast.

#include "core/abilities.hpp"
#include "core/archive.hpp"
#include "core/assert.hpp"
#include "core/frame.hpp"
#include "core/manifest.hpp"
#include "core/stable_id.hpp"
#include "core/state.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "core/volley.hpp"

#include <array>
#include <vector>

namespace game {

/// How many ticks a death's dissolve takes, from the hit that kills to the entity's removal: about half a second.
inline constexpr u64 DYING_TICKS = 30;
/// How many ticks after a fox's removal another takes its place.
inline constexpr u64 RESPAWN_TICKS = 180;
/// How many ticks a respawned fox takes to ease from the ground's edge into its orbit: about three seconds.
inline constexpr u32 ARRIVE_TICKS = 180;

/// The first of the source numbers (core::AbilityUse()) of the ability uses a cast starts, one per slot: the source of the
/// use of slot `s` is CAST_SOURCE + `s`. Below it are the aura's and the burst's.
inline constexpr u32 CAST_SOURCE = 16;

/// The ID of the use of the ability in `slot` that a cast at `tick` starts.
[[nodiscard]] constexpr u64 CastUse(u64 tick, core::Slot slot) {
    return core::AbilityUse(tick, CAST_SOURCE + static_cast<u32>(std::to_underlying(slot)));
}

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

/// What the player casts with: the Qi it holds and when each slot can next be cast.
struct Caster {
    /// The Qi held, from nothing to the most the game data gives.
    i64 qi = 0;
    /// The simulation tick each slot can next be cast at (core::Slot's order): a slot casts at a tick from this one on.
    std::array<u64, core::SLOTS> readyTick{};

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.qi);
        for (auto& ready : self.readyTick) {
            archive(ready);
        }
    }

    /// Whether a value read from a save can be used: no negative Qi.
    [[nodiscard]] bool Valid() const {
        return qi >= 0;
    }
};

/// A creature in a dash: its foot goes from `from` to `to`, eased in and out, over `ticks` ticks from `startTick` (the
/// first step on that tick, the last on `startTick + ticks - 1`), held movement ignored meanwhile.
struct Dashing {
    /// Where the dash started.
    core::Vec2 from{};
    /// Where it ends.
    core::Vec2 to{};
    /// The simulation tick it started.
    u64 startTick = 0;
    /// How many ticks it takes.
    u32 ticks = 0;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.from, self.to, self.startTick, self.ticks);
    }

    /// Whether a value read from a save can be used: finite ends and at least one tick.
    [[nodiscard]] bool Valid() const {
        return core::IsFinite(from) && core::IsFinite(to) && ticks > 0;
    }
};

/// How many ticks apart an affliction's ticks are, the first this long after the hit that left it: a simulation second.
inline constexpr u64 AFFLICTION_PERIOD = 60;

/// One affliction on a creature (a burn or a poison): what each of its ticks deals, how many are left, the ability use
/// they belong to and when the next is due.
struct AfflictionState {
    /// The amount each tick deals, as the cast resolved it (HitAmount()).
    i64 damage = 0;
    /// How many ticks are left; 0 when there is no such affliction.
    u32 ticksLeft = 0;
    /// The ability use whose hit left it (core::AbilityUse()): its ticks are hits of that use, which ends after them.
    u64 use = 0;
    /// The simulation tick its next tick is due.
    u64 nextTick = 0;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.damage, self.ticksLeft, self.use, self.nextTick);
    }

    /// Whether a value read from a save can be used: no negative damage.
    [[nodiscard]] bool Valid() const {
        return damage >= 0;
    }
};

/// The afflictions on a creature: one of each element at most, as a second application refreshes the first.
struct Afflictions {
    /// The fire's: its ticks are Burn hits.
    AfflictionState burn{};
    /// The poison's: its ticks are Poison hits.
    AfflictionState poison{};

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.burn, self.poison);
    }

    /// Whether a value read from a save can be used: both afflictions are.
    [[nodiscard]] bool Valid() const {
        return burn.Valid() && poison.Valid();
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

/// The arena's combat state beyond its components: the volleys in flight (the flying swords and the fire bolts), the
/// flashes showing (the poison lance's) and the respawns waiting.
class Combat {
public:
    /// Takes `amount` from `target`'s health (a negative amount heals, never past its most) at `tick`; health running
    /// out makes it Dying at `tick`. Does nothing to a target that isn't in `state`, has no Health, or is dying.
    static void Damage(core::State& state, core::StableId target, i64 amount, u64 tick);

    /// Casts the ability in `cast.slot` for `player` at `counters.simulation`, if it can: its slot holds one, the slot's
    /// cooldown is over, the point is finite and the player is alive. A cast during a dash is ignored, by design (logged at
    /// debug level). Order: the cooldown, then the Qi (too little appends a NotEnoughQi event with the slot as its value and
    /// changes nothing), then the ability's effect; a cast that goes off gains `qiGain` (never past the most), spends
    /// `qiCost` and starts the slot's cooldown. Does nothing for a `player` that isn't in `state` or has no Caster.
    ///
    /// The effects: the dash starts the player's Dashing. The flying swords tag every living enemy whose chest (its foot
    /// raised by half its sprite's foot height, from `manifest`) is within the ability's radius of the point, in stable
    /// ID order, and launch a volley of the data's count at them from the player's chest; the fire bolt launches a volley
    /// of one at the tagged enemy nearest the point, its flight ⌈distance / speed⌉ ticks, with no ring and no sweep. With
    /// none tagged, the volley's one target is the point itself. Either volley keeps its shape, damage and affliction as
    /// cast (each amount through HitAmount()), so new game data never changes it, and its commit window is the ability
    /// radius's (core::CommitTicksFor()). Its seed is drawn from core::Random with the state counter. The poison lance
    /// strikes in this tick's Strike(), after the afflictions due: every living enemy whose chest is then within its
    /// radius of the point, in stable ID order, takes its damage (as cast) as a hit of its element (Poison) at its head
    /// and, unless the hit killed it, gains its affliction (as a sword's hit leaves one, refreshing one there); a Flash of
    /// its element starts at the point. Its use ends then, with a UseEnded after its hits, unless an affliction it left
    /// holds it, and then after that affliction's last tick. The strike waits only from the cast to the Strike() of the
    /// same tick, so no save or hash ever holds it.
    void Cast(
        core::State& state,
        const core::Manifest& manifest,
        const core::Abilities& abilities,
        core::StableId player,
        const core::Action& cast,
        const core::TickCounters& counters,
        std::vector<core::Event>& events
    );

    /// Moves `player`, if it is dashing, to where its dash puts it at `tick` (a dash cast this tick takes its first step now),
    /// ending the dash at its last tick; whether it is dashing, which means the caller doesn't walk it.
    [[nodiscard]] static bool Dash(core::State& state, core::StableId player, u64 tick);

    /// Advances combat in the active domain of `state` to `tick`, after the orbits have moved: eases each Arriving
    /// creature from where it appeared towards where its orbit put it (which it reaches, ending its arrival, at its last
    /// tick); removes each creature DYING_TICKS after its death, queueing a fox's respawn RESPAWN_TICKS later; and
    /// returns how many foxes are due to respawn at `tick`, which the caller spawns.
    [[nodiscard]] u32 Tick(core::State& state, u64 tick);

    /// Advances the volleys and afflictions to `tick`, last in the tick, once everything else that moves or kills has.
    /// In order:
    /// - each living target's aim follows its creature's chest; a dead one's stays where it died;
    /// - each affliction due ticks: a hit of its element (Burn or Poison) and its use, at the creature's head;
    /// - each poison lance cast this tick strikes (Cast());
    /// - each sword arriving at `tick`, in sword order, strikes the target it flies at (core::SwordTarget()) if that is
    ///   alive as the tick began: a hit of the volley's element and use at the creature's head, and the volley's
    ///   affliction, refreshing one already there (whose use ends then if nothing else holds it). Otherwise it plunges,
    ///   with no event;
    /// - then each target whose creature died this tick, or is gone, is dead from `tick` on (core::VolleyTarget::diedTick),
    ///   and a dying creature's afflictions end;
    /// - each use whose last hit is behind it ends with a UseEnded event: a volley's at its last arrival unless it left an
    ///   affliction, else after that affliction's last tick, its creature's death or its refresh;
    /// - a volley whose last sword has faded is dropped, and a flash core::FLASH_TICKS after its start.
    void Strike(core::State& state, const core::Manifest& manifest, u64 tick, std::vector<core::Event>& events);

    /// Appends a UseEnded event for each use still open in `state`'s active domain (a volley with swords still to
    /// arrive, an affliction with ticks left), in use order: before a scene change throws them away.
    void EndUses(const core::State& state, std::vector<core::Event>& events) const;

    /// Adds each volley in flight, and its targets, to `snapshot`'s volleys and volley targets, and each flash showing
    /// to its flashes.
    void Publish(core::Snapshot& snapshot) const;

    /// The respawns waiting: the tick each is due, earliest first.
    [[nodiscard]] const std::vector<u64>& Respawns() const {
        return m_respawns;
    }

    /// Writes the combat state into `archive` (a Writer or a Hasher): the respawns waiting (their count, then each one's
    /// tick), then the volleys in flight (their count, then each one's cast, ability, element, damage, affliction, how
    /// many of its swords have arrived, and its targets' count and targets), then the flashes showing (their count, then
    /// each one).
    template<typename Archive>
    void Save(Archive& archive) const {
        PN_ASSERT(m_strikes.empty(), "a lance's strike outlived its tick's Strike()");
        archive(static_cast<u64>(m_respawns.size()));
        for (const u64 due : m_respawns) {
            archive(due);
        }
        archive(static_cast<u64>(m_flights.size()));
        for (const Flight& flight : m_flights) {
            archive(flight.volley, flight.ability, flight.element, flight.damage, flight.affliction, flight.resolved);
            archive(static_cast<u64>(flight.targets.size()));
            for (const core::VolleyTarget& target : flight.targets) {
                archive(target);
            }
        }
        archive(static_cast<u64>(m_flashes.size()));
        for (const core::Flash& flash : m_flashes) {
            archive(flash);
        }
    }

    /// Whether every living target of the volleys in flight (`diedTick` 0) names a creature in `state`'s active domain:
    /// what loaded volleys must satisfy with the state loaded beside them, as each tick reads a living target's body and
    /// look.
    [[nodiscard]] bool AimsAtCreatures(const core::State& state) const;

    /// Reads what Save() wrote, failing `archive` and changing nothing on a count past what is left, ticks out of order,
    /// a volley that can't be used (invalid (core::Volley::Valid()), of an unknown ability or element, a negative
    /// amount, more swords arrived than it has, no targets, a target at a point that isn't finite, or the point target
    /// (zero) not dead), or a flash at a point that isn't finite or of an unknown element. A living target is checked
    /// against the state loaded beside it by Arena::Load(), which refuses the save when one isn't a creature in its
    /// active domain (AimsAtCreatures()).
    void Load(core::Reader& archive);

private:
    // A volley in flight: the cast's record, which a save holds, and the order its swords arrive in, derived from it.
    struct Flight {
        core::Volley volley{};
        core::AbilityKind ability = core::AbilityKind::None;
        core::HitKind element = core::HitKind::Damage;
        i64 damage = 0;                // each hit's, as cast
        core::Affliction affliction{}; // what each hit leaves, its damage as cast; no ticks for none
        u32 resolved = 0;              // how many swords have arrived: the next is `order[resolved]`
        // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
        std::vector<core::VolleyTarget> targets{};
        // Derived, never saved: the swords in arrival order (ties in sword order), their arrival ticks, the last.
        // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
        std::vector<u32> order{};
        // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
        std::vector<u64> arrivals{};
        u64 lastArrival = 0;

        // Derives the arrival order from the record.
        void Prepare();
        // Whether its hits are still to come.
        [[nodiscard]] bool Pending() const {
            return resolved < volley.shape.count;
        }
    };

    void Start(
        core::State& state,
        const core::Manifest& manifest,
        core::Entity caster,
        const core::AbilityData& ability,
        core::AbilityKind kind,
        const core::Action& cast,
        const core::TickCounters& counters
    );
    // A poison lance cast this tick, which this tick's Strike() deals: the cast's record, its amounts as cast.
    struct LanceStrike {
        u64 use = 0;
        core::Vec2 point{};
        f32 radius = 0.0F;
        core::HitKind element = core::HitKind::Poison;
        i64 damage = 0;
        core::Affliction affliction{};
    };

    void Lance(const core::AbilityData& ability, const core::Action& cast, u64 tick);
    void DealStrike(
        core::State& state,
        const core::Manifest& manifest,
        const LanceStrike& strike,
        u64 tick,
        std::vector<core::Event>& events,
        std::vector<u64>& ending
    );
    void Launch(
        core::State& state,
        const core::Manifest& manifest,
        core::Entity caster,
        const core::AbilityData& ability,
        core::AbilityKind kind,
        const core::Action& cast,
        const core::TickCounters& counters
    );
    static void Arrive(
        core::State& state,
        const core::Manifest& manifest,
        const Flight& flight,
        u32 sword,
        u64 tick,
        std::vector<core::Event>& events,
        std::vector<u64>& ending
    );
    void FollowTargets(core::State& state, const core::Manifest& manifest);
    void MarkDeaths(const core::State& state, u64 tick);
    [[nodiscard]] bool Holds(const core::State& state, u64 use) const;
    // Ends each use in `ending` (sorted and made unique in place) that nothing holds any more (Holds()), with a UseEnded
    // event: the one queue every use's end goes through.
    void EndReleased(const core::State& state, std::vector<u64>& ending, u64 tick, std::vector<core::Event>& events) const;

    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<u64> m_respawns{};
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<Flight> m_flights{};
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<core::Flash> m_flashes{};
    // Cast this tick, struck by its Strike(): empty between ticks, so never saved.
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<LanceStrike> m_strikes{};
};

} // namespace game
