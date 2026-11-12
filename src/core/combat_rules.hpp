#pragma once

/// @file
/// @ingroup core
/// The combat rules (`assets/data/abilities.yaml`'s `combat:` section): the times and amounts the fight runs by that no
/// single ability owns. Game data: hashed, so a replay plays only under the rules it was recorded with.

#include "core/archive.hpp"
#include "core/types.hpp"

#include <cmath>

namespace core {

/// The shipped numbers of the combat rules (assets/data/abilities.yaml's `combat:` section), which CombatRules defaults to.
namespace shipped {

/// The shipped `dyingTicks` of CombatRules.
inline constexpr u64 COMBAT_DYING_TICKS = 30;
/// The shipped `respawnTicks` of CombatRules.
inline constexpr u64 COMBAT_RESPAWN_TICKS = 180;
/// The shipped `arriveTicks` of CombatRules.
inline constexpr u32 COMBAT_ARRIVE_TICKS = 180;
/// The shipped `afflictionPeriod` of CombatRules.
inline constexpr u64 COMBAT_AFFLICTION_PERIOD = 60;
/// The shipped `flashTicks` of CombatRules.
inline constexpr u64 COMBAT_FLASH_TICKS = 12;
/// The shipped `auraPeriod` of CombatRules.
inline constexpr u64 COMBAT_AURA_PERIOD = 60;
/// The shipped `auraReach` of CombatRules.
inline constexpr f32 COMBAT_AURA_REACH = 320.0F;
/// The shipped `auraDamageMin` of CombatRules.
inline constexpr u32 COMBAT_AURA_DAMAGE_MIN = 50;
/// The shipped `auraDamageMax` of CombatRules.
inline constexpr u32 COMBAT_AURA_DAMAGE_MAX = 400;
/// The shipped `auraHealingMin` of CombatRules.
inline constexpr u32 COMBAT_AURA_HEALING_MIN = 50;
/// The shipped `auraHealingMax` of CombatRules.
inline constexpr u32 COMBAT_AURA_HEALING_MAX = 200;

} // namespace shipped

/// How a fight runs. Times are simulation ticks; distances are world units. The defaults are the shipped numbers.
struct CombatRules {
    /// How many ticks a death's dissolve takes, from the hit that kills to the entity's removal: at least 1; about half a
    /// second.
    u64 dyingTicks = shipped::COMBAT_DYING_TICKS;
    /// How many ticks after a fox's removal another takes its place.
    u64 respawnTicks = shipped::COMBAT_RESPAWN_TICKS;
    /// How many ticks a respawned fox takes to ease from the ground's edge into its orbit, at least 1: about three seconds.
    u32 arriveTicks = shipped::COMBAT_ARRIVE_TICKS;
    /// How many ticks apart an affliction's ticks are, the first this long after the hit that left it, at least 1: a
    /// simulation second.
    u64 afflictionPeriod = shipped::COMBAT_AFFLICTION_PERIOD;
    /// How many ticks the poison lance's burst lasts, at least 1 (Flash::ticks).
    u64 flashTicks = shipped::COMBAT_FLASH_TICKS;
    /// How many ticks apart the crystal's aura hits, at least 1.
    u64 auraPeriod = shipped::COMBAT_AURA_PERIOD;
    /// How far from the crystal's foot the aura reaches, in world units.
    f32 auraReach = shipped::COMBAT_AURA_REACH;
    /// The least damage one aura hit deals a fox, drawn evenly between it and `auraDamageMax`.
    u32 auraDamageMin = shipped::COMBAT_AURA_DAMAGE_MIN;
    /// The most damage one aura hit deals a fox; at least `auraDamageMin`.
    u32 auraDamageMax = shipped::COMBAT_AURA_DAMAGE_MAX;
    /// The least health one aura hit heals the cultivator, drawn evenly between it and `auraHealingMax`.
    u32 auraHealingMin = shipped::COMBAT_AURA_HEALING_MIN;
    /// The most health one aura hit heals the cultivator; at least `auraHealingMin`.
    u32 auraHealingMax = shipped::COMBAT_AURA_HEALING_MAX;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(
            self.dyingTicks,
            self.respawnTicks,
            self.arriveTicks,
            self.afflictionPeriod,
            self.flashTicks,
            self.auraPeriod,
            self.auraReach,
            self.auraDamageMin,
            self.auraDamageMax,
            self.auraHealingMin,
            self.auraHealingMax
        );
    }

    /// Whether every time that divides or paces something is at least one tick, the reach is finite and not negative, and
    /// each aura range runs from its least to its most; what rules read from the data must satisfy.
    [[nodiscard]] bool Valid() const {
        return dyingTicks >= 1 && arriveTicks >= 1 && afflictionPeriod >= 1 && flashTicks >= 1 && auraPeriod >= 1 &&
               std::isfinite(auraReach) && auraReach >= 0.0F && auraDamageMin <= auraDamageMax &&
               auraHealingMin <= auraHealingMax;
    }
};

} // namespace core
