#pragma once

/// @file
/// @ingroup data
/// The abilities table (design doc 0002.3 §7): `assets/data/abilities.yaml` into core::Abilities.

#include "core/abilities.hpp"
#include "core/combat_rules.hpp"
#include "core/error.hpp"

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace data {

/// The abilities `abilities` (abilities.yaml's text) holds. Fails, naming the line where it can, on YAML that doesn't
/// parse, an ability, slot, element or kind it doesn't know, a number missing, negative, not finite or not whole where
/// it must be, or an ability in a slot of another kind. Seconds become simulation ticks (rounded, at least one tick
/// where above zero); a speed in world units a second becomes units a tick. A projectile ability's `motion:` block (refused on
/// an ability with neither swords nor a speed) (aim_scatter, short_bend, turn, arrival_speed, retarget in seconds,
/// retarget_speed in world units a second) sets how its swords or bolts fly; a key it leaves out keeps the shape's shipped
/// default. Its `seal:` block (refused likewise) names the seal a cast raises, kept as its key (core::SealKeyOf()), and its
/// size (above 0); one without a name is refused. A key it doesn't know is ignored, with a line naming it added to
/// `warnings` when given.
[[nodiscard]] std::expected<core::Abilities, core::Error> ParseAbilities(
    std::string_view abilities,
    std::vector<std::string>* warnings = nullptr
);

/// Everything abilities.yaml holds: the abilities and the combat rules.
struct AbilitiesFile {
    /// The abilities and the loadout.
    core::Abilities abilities;
    /// The `combat:` section.
    core::CombatRules combat;
};

/// The abilities and the combat rules `abilities` (abilities.yaml's text) holds, parsing the text once: what
/// ParseAbilities() and ParseCombatRules() refuse, this refuses, the abilities first.
[[nodiscard]] std::expected<AbilitiesFile, core::Error> ParseAbilitiesFile(
    std::string_view abilities,
    std::vector<std::string>* warnings = nullptr
);

/// The combat rules `abilities` (abilities.yaml's text) holds in its `combat:` section: the dissolve, respawn and arrival
/// times, the affliction period and the burst's length (seconds, as ticks), and the aura's period, reach and damage and
/// healing ranges. A section or key left out keeps the shipped number (core::CombatRules). Fails, naming the line where
/// it can, on YAML that doesn't parse or a value that isn't a number, is negative, is a time of under one tick where it
/// needs one, or a range from more to less. A key it doesn't know is ignored, with a line naming it added to `warnings`
/// when given.
[[nodiscard]] std::expected<core::CombatRules, core::Error> ParseCombatRules(
    std::string_view abilities,
    std::vector<std::string>* warnings = nullptr
);

} // namespace data
