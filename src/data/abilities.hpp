#pragma once

/// @file
/// @ingroup data
/// The abilities table (design doc 0002.3 §7): `assets/data/abilities.yaml` into core::Abilities.

#include "core/abilities.hpp"
#include "core/error.hpp"

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace data {

/// The abilities `abilities` (abilities.yaml's text) holds. Fails, naming the line where it can, on YAML that doesn't
/// parse, an ability, slot, element or kind it doesn't know, a number missing, negative, not finite or not whole where
/// it must be, or an ability in a slot of another kind. Seconds become simulation ticks (rounded, at least one tick
/// where above zero); a speed in world units a second becomes units a tick. A key it doesn't know is ignored, with a
/// line naming it added to `warnings` when given.
[[nodiscard]] std::expected<core::Abilities, core::Error> ParseAbilities(
    std::string_view abilities,
    std::vector<std::string>* warnings = nullptr
);

} // namespace data
