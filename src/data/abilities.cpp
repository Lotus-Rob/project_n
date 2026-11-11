#include "data/abilities.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/tick_clock.hpp"
#include "data/internal/yaml.hpp"

namespace data {

namespace {

constexpr std::array<std::string_view, core::HIT_KINDS> ELEMENT_NAMES{ { "damage", "healing", "poison", "burn" } };
// Every whole number below this is exact in an f64.
constexpr i64 MAX_WHOLE = 9'007'199'254'740'992;
constexpr f64 MAX_SECONDS = 1'000'000.0;
constexpr i64 MAX_AFFLICTION_TICKS = 1'000'000;
constexpr f64 TICKS = static_cast<f64>(core::TICKS_PER_SECOND);

// Named for its file: game_data.cpp and manifest.cpp have their own, and a unity build puts them in one translation
// unit.
std::unexpected<core::Error> RefuseAbilities(const YAML::Node& node, std::string_view why) {
    return std::unexpected{ core::Error{ std::format("abilities line {}: {}", node.Mark().line + 1, why) } };
}

// The first error among `results`, if any.
template<typename... T>
std::optional<core::Error> FirstError(const std::expected<T, core::Error>&... results) {
    std::optional<core::Error> first;
    const auto note = [&first](const auto& result) {
        if (!first && !result) {
            first = result.error();
        }
    };
    (note(results), ...);
    return first;
}

// `name`'s number at `key` in `map` (0 when absent and not `required`), finite and not negative; yaml-cpp throws on
// text that isn't a number.
std::expected<f64, core::Error> Number(const YAML::Node& map, std::string_view name, const char* key, bool required) {
    const YAML::Node node = map[key];
    if (!node) {
        if (required) {
            return RefuseAbilities(map, std::format("{} has no {}", name, key));
        }
        return 0.0;
    }
    f64 value = 0.0;
    try {
        value = node.as<f64>();
    }
    catch (const YAML::Exception&) {
        return RefuseAbilities(node, std::format("{}'s {} isn't a number", name, key));
    }
    if (!std::isfinite(value) || value < 0.0) {
        return RefuseAbilities(node, std::format("{}'s {} isn't a finite number of 0 or more", name, key));
    }
    return value;
}

std::expected<i64, core::Error> Amount(const YAML::Node& map, std::string_view name, const char* key, bool required) {
    return internal::WholeNumber("abilities", map, name, key, required, MAX_WHOLE);
}

std::expected<u32, core::Error> Count(const YAML::Node& map, std::string_view name, const char* key, bool required, i64 limit) {
    return internal::WholeNumber("abilities", map, name, key, required, limit).transform([](i64 value) {
        return static_cast<u32>(value);
    });
}

std::expected<f32, core::Error> Distance(const YAML::Node& map, std::string_view name, const char* key) {
    return Number(map, name, key, false).transform([](f64 value) { return static_cast<f32>(value); });
}

// Seconds as ticks: rounded, at least one where above zero.
std::expected<u32, core::Error> Seconds(const YAML::Node& map, std::string_view name, const char* key, bool required) {
    return Number(map, name, key, required).and_then([&](f64 value) -> std::expected<u32, core::Error> {
        if (value > MAX_SECONDS) {
            return RefuseAbilities(map[key], std::format("{}'s {} is more than {} seconds", name, key, MAX_SECONDS));
        }
        return value > 0.0 ? static_cast<u32>(std::max(1.0, std::round(value * TICKS))) : 0U;
    });
}

std::optional<std::size_t> IndexOf(std::span<const std::string_view> names, std::string_view name) {
    const auto found = std::ranges::find(names, name);
    if (found == names.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - names.begin());
}

std::expected<core::Affliction, core::Error> AfflictionAt(
    const YAML::Node& node,
    const std::string& name,
    std::vector<std::string>* warnings
) {
    if (!node.IsMap()) {
        return RefuseAbilities(node, std::format("{} isn't a map", name));
    }
    internal::NoteUnknownKeys("abilities", node, name, "key", { "ticks", "damage" }, warnings);
    const std::expected<u32, core::Error> ticks = Count(node, name, "ticks", true, MAX_AFFLICTION_TICKS);
    const std::expected<i64, core::Error> damage = Amount(node, name, "damage", true);
    if (const std::optional<core::Error> error = FirstError(ticks, damage)) {
        return std::unexpected{ *error };
    }
    return core::Affliction{ .ticks = *ticks, .damage = *damage };
}

std::expected<core::VolleyShape, core::Error> Swords(
    const YAML::Node& node,
    const std::string& name,
    std::vector<std::string>* warnings
) {
    if (!node.IsMap()) {
        return RefuseAbilities(node, std::format("{} isn't a map", name));
    }
    internal::NoteUnknownKeys(
        "abilities", node, name, "key", { "count", "ring_radius", "stagger", "out", "flight", "sweep", "fade" }, warnings
    );
    const std::expected<u32, core::Error> count = Count(node, name, "count", true, core::MAX_VOLLEY_COUNT);
    const std::expected<f32, core::Error> ring = Distance(node, name, "ring_radius");
    const std::expected<u32, core::Error> stagger = Seconds(node, name, "stagger", true);
    const std::expected<u32, core::Error> out = Seconds(node, name, "out", true);
    const std::expected<u32, core::Error> flight = Seconds(node, name, "flight", true);
    const std::expected<f32, core::Error> sweep = Distance(node, name, "sweep");
    const std::expected<u32, core::Error> fade = Seconds(node, name, "fade", true);
    if (const std::optional<core::Error> error = FirstError(count, ring, stagger, out, flight, sweep, fade)) {
        return std::unexpected{ *error };
    }
    return core::VolleyShape{ .count = *count,
                              .ringRadius = *ring,
                              .staggerTicks = *stagger,
                              .outTicks = *out,
                              .flightTicks = *flight,
                              .sweep = *sweep,
                              .fadeTicks = *fade };
}

std::expected<core::AbilityData, core::Error> Ability(
    const YAML::Node& node,
    const std::string& name,
    std::vector<std::string>* warnings
) {
    if (!node.IsMap()) {
        return RefuseAbilities(node, std::format("{} isn't a map", name));
    }
    internal::NoteUnknownKeys(
        "abilities",
        node,
        name,
        "key",
        { "slot",
          "cooldown",
          "qi_cost",
          "qi_gain",
          "damage",
          "element",
          "radius",
          "speed",
          "distance",
          "duration",
          "burn",
          "poison",
          "swords" },
        warnings
    );
    core::AbilityData data;
    const YAML::Node slot = node["slot"];
    if (!slot) {
        return RefuseAbilities(node, std::format("{} has no slot", name));
    }
    const auto slotName = slot.as<std::string>();
    const std::optional<std::size_t> kind = IndexOf(core::SLOT_KIND_NAMES, slotName);
    if (!kind) {
        return RefuseAbilities(
            slot, std::format("{}'s slot {} isn't generator, spender, movement, special or ultimate", name, slotName)
        );
    }
    data.slot = static_cast<core::SlotKind>(*kind);
    if (const YAML::Node element = node["element"]) {
        const auto elementName = element.as<std::string>();
        const std::optional<std::size_t> hit = IndexOf(ELEMENT_NAMES, elementName);
        if (!hit) {
            return RefuseAbilities(
                element, std::format("{}'s element {} isn't damage, healing, poison or burn", name, elementName)
            );
        }
        data.hit = static_cast<core::HitKind>(*hit);
    }
    const std::expected<u32, core::Error> cooldown = Seconds(node, name, "cooldown", true);
    const std::expected<u32, core::Error> duration = Seconds(node, name, "duration", false);
    const std::expected<i64, core::Error> qiCost = Amount(node, name, "qi_cost", false);
    const std::expected<i64, core::Error> qiGain = Amount(node, name, "qi_gain", false);
    const std::expected<i64, core::Error> damage = Amount(node, name, "damage", false);
    const std::expected<f32, core::Error> radius = Distance(node, name, "radius");
    const std::expected<f32, core::Error> distance = Distance(node, name, "distance");
    const std::expected<f32, core::Error> speed = Distance(node, name, "speed");
    if (const std::optional<core::Error> error =
            FirstError(cooldown, duration, qiCost, qiGain, damage, radius, distance, speed)) {
        return std::unexpected{ *error };
    }
    data.cooldownTicks = *cooldown;
    data.durationTicks = *duration;
    data.qiCost = *qiCost;
    data.qiGain = *qiGain;
    data.damage = *damage;
    data.radius = *radius;
    data.distance = *distance;
    if (data.slot == core::SlotKind::Movement && (data.distance <= 0.0F || data.durationTicks == 0)) {
        return RefuseAbilities(node, std::format("{} is a movement ability and needs a distance and a duration above 0", name));
    }
    data.speed = *speed / static_cast<f32>(core::TICKS_PER_SECOND);
    const YAML::Node burn = node["burn"];
    const YAML::Node poison = node["poison"];
    if (burn && poison) {
        return RefuseAbilities(poison, std::format("{} has both burn and poison", name));
    }
    if (const YAML::Node over = burn ? burn : poison) {
        const std::expected<core::Affliction, core::Error> affliction =
            AfflictionAt(over, std::format("{}'s {}", name, burn ? "burn" : "poison"), warnings);
        if (!affliction) {
            return std::unexpected{ affliction.error() };
        }
        data.affliction = *affliction;
    }
    if (const YAML::Node swords = node["swords"]) {
        const std::expected<core::VolleyShape, core::Error> shape = Swords(swords, std::format("{}'s swords", name), warnings);
        if (!shape) {
            return std::unexpected{ shape.error() };
        }
        data.volley = *shape;
    }
    return data;
}

std::expected<void, core::Error> Loadout(
    const YAML::Node& node,
    const std::array<bool, core::ABILITY_KINDS>& defined,
    core::Abilities& abilities
) {
    if (!node.IsMap()) {
        return RefuseAbilities(node, "the loadout isn't a map");
    }
    for (const auto& entry : node) {
        const auto slotName = entry.first.as<std::string>();
        const std::optional<std::size_t> slot = IndexOf(core::SLOT_NAMES, slotName);
        if (!slot) {
            return RefuseAbilities(entry.first, std::format("the loadout has no slot {}", slotName));
        }
        const auto abilityName = entry.second.as<std::string>();
        const std::optional<std::size_t> ability = IndexOf(core::ABILITY_NAMES, abilityName);
        if (!ability || *ability == 0) {
            return RefuseAbilities(entry.second, std::format("{} in the loadout isn't an ability", abilityName));
        }
        if (!defined.at(*ability)) {
            return RefuseAbilities(entry.second, std::format("{} in the loadout isn't defined in the abilities", abilityName));
        }
        const core::SlotKind slotKind = core::KindOf(static_cast<core::Slot>(*slot));
        const core::SlotKind abilityKind = abilities.table.at(*ability).slot;
        if (slotKind != abilityKind) {
            return RefuseAbilities(
                entry.second,
                std::format(
                    "slot {} takes a {} ability, but {} is a {} ability",
                    slotName,
                    core::SLOT_KIND_NAMES.at(std::to_underlying(slotKind)),
                    abilityName,
                    core::SLOT_KIND_NAMES.at(std::to_underlying(abilityKind))
                )
            );
        }
        abilities.loadout.at(*slot) = static_cast<core::AbilityKind>(*ability);
    }
    return {};
}

std::expected<core::Abilities, core::Error> Read(const YAML::Node& root, std::vector<std::string>* warnings) {
    if (!root.IsMap()) {
        return RefuseAbilities(root, "not a map of abilities");
    }
    internal::NoteUnknownKeys("abilities", root, "", "section", { "qi", "abilities", "loadout" }, warnings);
    core::Abilities abilities;
    const YAML::Node qiNode = root["qi"];
    if (!qiNode || !qiNode.IsMap()) {
        return RefuseAbilities(qiNode ? qiNode : root, "there is no qi");
    }
    internal::NoteUnknownKeys("abilities", qiNode, "qi", "key", { "max", "start" }, warnings);
    const std::expected<i64, core::Error> qiMax = Amount(qiNode, "qi", "max", true);
    const std::expected<i64, core::Error> qiStart = Amount(qiNode, "qi", "start", true);
    if (const std::optional<core::Error> error = FirstError(qiMax, qiStart)) {
        return std::unexpected{ *error };
    }
    if (*qiStart > *qiMax) {
        return RefuseAbilities(qiNode, "qi's start is more than its max");
    }
    abilities.qiMax = *qiMax;
    abilities.qiStart = *qiStart;
    std::array<bool, core::ABILITY_KINDS> defined{};
    if (const YAML::Node list = root["abilities"]) {
        if (!list.IsMap()) {
            return RefuseAbilities(list, "the abilities aren't a map");
        }
        for (const auto& entry : list) {
            const auto name = entry.first.as<std::string>();
            const std::optional<std::size_t> kind = IndexOf(core::ABILITY_NAMES, name);
            if (!kind || *kind == 0) {
                return RefuseAbilities(entry.first, std::format("{} isn't an ability", name));
            }
            const std::expected<core::AbilityData, core::Error> data = Ability(entry.second, name, warnings);
            if (!data) {
                return std::unexpected{ data.error() };
            }
            abilities.table.at(*kind) = *data;
            defined.at(*kind) = true;
        }
    }
    if (const YAML::Node loadout = root["loadout"]) {
        if (const std::expected<void, core::Error> placed = Loadout(loadout, defined, abilities); !placed) {
            return std::unexpected{ placed.error() };
        }
    }
    return abilities;
}

} // namespace

std::expected<core::Abilities, core::Error> ParseAbilities(std::string_view abilities, std::vector<std::string>* warnings) {
    try {
        return Read(YAML::Load(std::string{ abilities }), warnings);
    }
    catch (const YAML::Exception& error) {
        return std::unexpected{ internal::ErrorOf("abilities", error) };
    }
}

} // namespace data
