#include "data/abilities.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/tick_clock.hpp"
#include "data/internal/yaml.hpp"

namespace data {

namespace {

constexpr std::array<std::string_view, core::HIT_KINDS> ELEMENT_NAMES{ { "damage", "healing", "poison", "burn" } };
// Every whole number below this is exact in an f64.
constexpr i64 MAX_WHOLE = 9'007'199'254'740'992;
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

std::expected<i64, core::Error> Amount(const YAML::Node& map, std::string_view name, const char* key, bool required) {
    return internal::WholeNumber("abilities", map, name, key, required, MAX_WHOLE);
}

std::expected<u32, core::Error> Count(const YAML::Node& map, std::string_view name, const char* key, bool required, i64 limit) {
    return internal::WholeNumber("abilities", map, name, key, required, limit).transform([](i64 value) {
        return static_cast<u32>(value);
    });
}

// `name`'s number at `key` as an f32 (a distance, a speed or an angle), finite and not negative; 0 when absent and not
// `required`.
std::expected<f32, core::Error> NonNegative(
    const YAML::Node& map,
    std::string_view name,
    const char* key,
    bool required = false
) {
    const std::expected<f64, core::Error> value = required
                                                      ? internal::RequiredNumber("abilities", map, name, key, { .low = 0.0 })
                                                      : internal::TunedNumber("abilities", map, name, key, 0.0, { .low = 0.0 });
    return value.transform([](f64 number) { return static_cast<f32>(number); });
}

// A required fraction from 0 to below 1.
std::expected<f32, core::Error> Fraction(const YAML::Node& map, std::string_view name, const char* key) {
    return NonNegative(map, name, key, true).and_then([&](f32 value) -> std::expected<f32, core::Error> {
        if (value >= 1.0F) {
            return RefuseAbilities(map[key], std::format("{}'s {} isn't below 1", name, key));
        }
        return value;
    });
}

std::optional<std::size_t> IndexOf(std::span<const std::string_view> names, std::string_view name) {
    const auto found = std::ranges::find(names, name);
    if (found == names.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - names.begin());
}

// Reads the `motion:` block of `name` (an ability) onto `shape`: each key it leaves out keeps the shape's shipped default.
std::expected<void, core::Error> Motion(
    const YAML::Node& node,
    const std::string& name,
    std::vector<std::string>* warnings,
    core::VolleyShape& shape
) {
    if (!node.IsMap()) {
        return RefuseAbilities(node, std::format("{} isn't a map", name));
    }
    internal::NoteUnknownKeys(
        "abilities",
        node,
        name,
        "key",
        { "aim_scatter", "short_bend", "turn", "arrival_speed", "retarget", "retarget_speed" },
        warnings
    );
    const std::expected<f64, core::Error> aimScatter =
        internal::TunedNumber("abilities", node, name, "aim_scatter", static_cast<f64>(shape.aimScatter), { .low = 0.0 });
    const std::expected<f64, core::Error> shortBend = internal::TunedNumber(
        "abilities", node, name, "short_bend", static_cast<f64>(shape.shortBend), { .low = 0.0, .lowOpen = true }
    );
    const std::expected<u32, core::Error> turn = internal::TunedTicks("abilities", node, name, "turn", shape.turnTicks, 0);
    const std::expected<f64, core::Error> arrivalSpeed = internal::TunedNumber(
        "abilities",
        node,
        name,
        "arrival_speed",
        static_cast<f64>(shape.arrivalSpeed),
        { .low = 0.0, .lowOpen = false, .high = static_cast<f64>(core::MAX_ARRIVAL_SPEED) }
    );
    const std::expected<u32, core::Error> retarget =
        internal::TunedTicks("abilities", node, name, "retarget", shape.retargetTicks, 1);
    const std::expected<f64, core::Error> retargetSpeed = internal::TunedNumber(
        "abilities",
        node,
        name,
        "retarget_speed",
        static_cast<f64>(shape.retargetSpeed) * TICKS,
        { .low = 0.0, .lowOpen = true }
    );
    if (const std::optional<core::Error> error =
            FirstError(aimScatter, shortBend, turn, arrivalSpeed, retarget, retargetSpeed)) {
        return std::unexpected{ *error };
    }
    shape.aimScatter = static_cast<f32>(*aimScatter);
    shape.shortBend = static_cast<f32>(*shortBend);
    shape.turnTicks = *turn;
    shape.arrivalSpeed = static_cast<f32>(*arrivalSpeed);
    shape.retargetTicks = *retarget;
    shape.retargetSpeed = static_cast<f32>(*retargetSpeed / TICKS);
    return {};
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

// Reads a `seal:` block: the seal's name, which game data keeps only as its key, and its size.
std::expected<core::SealCast, core::Error> SealAt(
    const YAML::Node& node,
    const std::string& name,
    std::vector<std::string>* warnings
) {
    if (!node.IsMap()) {
        return RefuseAbilities(node, std::format("{} isn't a map", name));
    }
    internal::NoteUnknownKeys("abilities", node, name, "key", { "name", "size" }, warnings);
    const YAML::Node sealName = node["name"];
    // A null (`name:` or `~`) reads as the text "null", and a list or a map isn't a name: only a scalar's text counts.
    const std::string text = sealName && sealName.IsScalar() ? sealName.Scalar() : std::string{};
    if (text.empty()) {
        return RefuseAbilities(sealName ? sealName : node, std::format("{} has no name", name));
    }
    constexpr internal::Range ABOVE_ZERO{ .low = 0.0,
                                          .lowOpen = true,
                                          .high = static_cast<f64>(std::numeric_limits<f32>::max()) };
    const std::expected<f64, core::Error> size = internal::RequiredNumber("abilities", node, name, "size", ABOVE_ZERO);
    if (!size) {
        return std::unexpected{ size.error() };
    }
    const core::SealCast cast{ .key = core::SealKeyOf(text), .size = static_cast<f32>(*size) };
    if (!cast.Valid()) { // a size above 0 too small for an f32
        return RefuseAbilities(node["size"], std::format("{}'s size is too small", name));
    }
    return cast;
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
        "abilities",
        node,
        name,
        "key",
        { "count",
          "waves",
          "wave_gap",
          "ring_radius",
          "ring_step",
          "scatter",
          "angle_jitter",
          "stagger",
          "out",
          "hold",
          "depart_jitter",
          "flight",
          "sweep",
          "sweep_jitter",
          "fade" },
        warnings
    );
    const std::expected<u32, core::Error> count = Count(node, name, "count", true, core::MAX_VOLLEY_COUNT);
    const std::expected<u32, core::Error> waves =
        Count(node, name, "waves", true, core::MAX_VOLLEY_COUNT).and_then([&](u32 value) -> std::expected<u32, core::Error> {
            if (value == 0) {
                return RefuseAbilities(node["waves"], std::format("{}'s waves is 0; a volley forms at least one", name));
            }
            return value;
        });
    const std::expected<u32, core::Error> waveGap = internal::RequiredTicks("abilities", node, name, "wave_gap", 0);
    const std::expected<f32, core::Error> ring = NonNegative(node, name, "ring_radius");
    const std::expected<f32, core::Error> ringStep = NonNegative(node, name, "ring_step", true);
    const std::expected<f32, core::Error> scatter = Fraction(node, name, "scatter");
    const std::expected<f32, core::Error> angleJitter = NonNegative(node, name, "angle_jitter", true);
    const std::expected<u32, core::Error> stagger = internal::RequiredTicks("abilities", node, name, "stagger", 0);
    const std::expected<u32, core::Error> out = internal::RequiredTicks("abilities", node, name, "out", 0);
    const std::expected<u32, core::Error> hold = internal::RequiredTicks("abilities", node, name, "hold", 0);
    const std::expected<u32, core::Error> departJitter = internal::RequiredTicks("abilities", node, name, "depart_jitter", 0);
    const std::expected<u32, core::Error> flight = internal::RequiredTicks("abilities", node, name, "flight", 0);
    const std::expected<f32, core::Error> sweep = NonNegative(node, name, "sweep");
    const std::expected<f32, core::Error> sweepJitter = Fraction(node, name, "sweep_jitter");
    const std::expected<u32, core::Error> fade = internal::RequiredTicks("abilities", node, name, "fade", 0);
    if (const std::optional<core::Error> error = FirstError(
            count,
            waves,
            waveGap,
            ring,
            ringStep,
            scatter,
            angleJitter,
            stagger,
            out,
            hold,
            departJitter,
            flight,
            sweep,
            sweepJitter,
            fade
        )) {
        return std::unexpected{ *error };
    }
    return core::VolleyShape{ .count = *count,
                              .waves = *waves,
                              .ringRadius = *ring,
                              .ringStep = *ringStep,
                              .scatter = *scatter,
                              .angleJitter = *angleJitter,
                              .staggerTicks = *stagger,
                              .waveGapTicks = *waveGap,
                              .outTicks = *out,
                              .holdTicks = *hold,
                              .departJitterTicks = *departJitter,
                              .flightTicks = *flight,
                              .sweep = *sweep,
                              .sweepJitter = *sweepJitter,
                              .fadeTicks = *fade };
}

// Reads the `swords:` and `motion:` blocks of `name` (an ability) into `data.volley`.
std::expected<void, core::Error> Flight(
    const YAML::Node& node,
    const std::string& name,
    std::vector<std::string>* warnings,
    core::AbilityData& data
) {
    if (const YAML::Node swords = node["swords"]) {
        const std::expected<core::VolleyShape, core::Error> shape = Swords(swords, std::format("{}'s swords", name), warnings);
        if (!shape) {
            return std::unexpected{ shape.error() };
        }
        data.volley = *shape;
    }
    if (const YAML::Node motion = node["motion"]) {
        // A known key where nothing reads it is refused, as a dash without its distance is; an unknown one only warns.
        if (!node["swords"] && data.speed <= 0.0F) {
            return RefuseAbilities(motion, std::format("{} has a motion but flies nothing: it needs swords or a speed", name));
        }
        if (const std::expected<void, core::Error> read =
                Motion(motion, std::format("{}'s motion", name), warnings, data.volley);
            !read) {
            return std::unexpected{ read.error() };
        }
    }
    return {};
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
          "swords",
          "motion",
          "seal" },
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
    const std::expected<u32, core::Error> cooldown = internal::RequiredTicks("abilities", node, name, "cooldown", 0);
    const std::expected<u32, core::Error> duration = internal::TunedTicks("abilities", node, name, "duration", 0, 0);
    const std::expected<i64, core::Error> qiCost = Amount(node, name, "qi_cost", false);
    const std::expected<i64, core::Error> qiGain = Amount(node, name, "qi_gain", false);
    const std::expected<i64, core::Error> damage = Amount(node, name, "damage", false);
    const std::expected<f32, core::Error> radius = NonNegative(node, name, "radius");
    const std::expected<f32, core::Error> distance = NonNegative(node, name, "distance");
    const std::expected<f32, core::Error> speed = NonNegative(node, name, "speed");
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
    if (const std::expected<void, core::Error> flies = Flight(node, name, warnings, data); !flies) {
        return std::unexpected{ flies.error() };
    }
    if (const YAML::Node seal = node["seal"]) {
        // Only a volley raises its seal: one anywhere else would never show, so it is refused, as a motion there is.
        if (!node["swords"] && data.speed <= 0.0F) {
            return RefuseAbilities(seal, std::format("{} has a seal but flies nothing: it needs swords or a speed", name));
        }
        const std::expected<core::SealCast, core::Error> cast = SealAt(seal, std::format("{}'s seal", name), warnings);
        if (!cast) {
            return std::unexpected{ cast.error() };
        }
        data.seal = *cast;
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
    internal::NoteUnknownKeys("abilities", root, "", "section", { "qi", "abilities", "loadout", "combat" }, warnings);
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

// Reads the `combat:` section; each key it leaves out keeps the shipped default of core::CombatRules.
std::expected<core::CombatRules, core::Error> Combat(const YAML::Node& node, std::vector<std::string>* warnings) {
    if (!node.IsMap()) {
        return RefuseAbilities(node, "the combat rules aren't a map");
    }
    internal::NoteUnknownKeys(
        "abilities", node, "combat", "key", { "dying", "respawn", "arrive", "affliction_period", "flash", "aura" }, warnings
    );
    core::CombatRules rules;
    const auto ticks = [&](const YAML::Node& map, std::string_view name, const char* key, u64 fallback, u32 least) {
        return internal::TunedTicks("abilities", map, name, key, static_cast<u32>(fallback), least);
    };
    const std::expected<u32, core::Error> dying = ticks(node, "combat", "dying", rules.dyingTicks, 1);
    const std::expected<u32, core::Error> respawn = ticks(node, "combat", "respawn", rules.respawnTicks, 0);
    const std::expected<u32, core::Error> arrive = ticks(node, "combat", "arrive", rules.arriveTicks, 1);
    const std::expected<u32, core::Error> period = ticks(node, "combat", "affliction_period", rules.afflictionPeriod, 1);
    const std::expected<u32, core::Error> flash = ticks(node, "combat", "flash", rules.flashTicks, 1);
    if (const std::optional<core::Error> error = FirstError(dying, respawn, arrive, period, flash)) {
        return std::unexpected{ *error };
    }
    rules.dyingTicks = *dying;
    rules.respawnTicks = *respawn;
    rules.arriveTicks = *arrive;
    rules.afflictionPeriod = *period;
    rules.flashTicks = *flash;
    const YAML::Node aura = node["aura"];
    if (!aura) {
        return rules;
    }
    if (!aura.IsMap()) {
        return RefuseAbilities(aura, "the combat's aura isn't a map");
    }
    internal::NoteUnknownKeys(
        "abilities",
        aura,
        "combat's aura",
        "key",
        { "period", "reach", "damage_min", "damage_max", "healing_min", "healing_max" },
        warnings
    );
    constexpr std::string_view AURA = "combat's aura";
    constexpr i64 MOST_AMOUNT = std::numeric_limits<u32>::max();
    const std::expected<u32, core::Error> auraPeriod = ticks(aura, AURA, "period", rules.auraPeriod, 1);
    const std::expected<f64, core::Error> reach =
        internal::TunedNumber("abilities", aura, AURA, "reach", static_cast<f64>(rules.auraReach), { .low = 0.0 });
    const auto amount = [&](const char* key, u32 fallback) {
        return internal::TunedWhole("abilities", aura, AURA, key, fallback, MOST_AMOUNT).transform([](i64 value) {
            return static_cast<u32>(value);
        });
    };
    const std::expected<u32, core::Error> damageMin = amount("damage_min", rules.auraDamageMin);
    const std::expected<u32, core::Error> damageMax = amount("damage_max", rules.auraDamageMax);
    const std::expected<u32, core::Error> healingMin = amount("healing_min", rules.auraHealingMin);
    const std::expected<u32, core::Error> healingMax = amount("healing_max", rules.auraHealingMax);
    if (const std::optional<core::Error> error = FirstError(auraPeriod, reach, damageMin, damageMax, healingMin, healingMax)) {
        return std::unexpected{ *error };
    }
    if (*damageMin > *damageMax) {
        return RefuseAbilities(aura, "the combat's aura has a damage_min above its damage_max");
    }
    if (*healingMin > *healingMax) {
        return RefuseAbilities(aura, "the combat's aura has a healing_min above its healing_max");
    }
    rules.auraPeriod = *auraPeriod;
    rules.auraReach = static_cast<f32>(*reach);
    rules.auraDamageMin = *damageMin;
    rules.auraDamageMax = *damageMax;
    rules.auraHealingMin = *healingMin;
    rules.auraHealingMax = *healingMax;
    return rules;
}

} // namespace

namespace {

// The combat rules in `root`'s `combat:` section, or the shipped ones with none.
std::expected<core::CombatRules, core::Error> RulesOf(const YAML::Node& root, std::vector<std::string>* warnings) {
    if (!root.IsMap()) {
        return RefuseAbilities(root, "not a map of abilities");
    }
    const YAML::Node combat = root["combat"];
    if (!combat) {
        return core::CombatRules{};
    }
    return Combat(combat, warnings);
}

} // namespace

std::expected<core::CombatRules, core::Error> ParseCombatRules(std::string_view abilities, std::vector<std::string>* warnings) {
    try {
        return RulesOf(YAML::Load(std::string{ abilities }), warnings);
    }
    catch (const YAML::Exception& error) {
        return std::unexpected{ internal::ErrorOf("abilities", error) };
    }
}

std::expected<AbilitiesFile, core::Error> ParseAbilitiesFile(std::string_view abilities, std::vector<std::string>* warnings) {
    try {
        const YAML::Node root = YAML::Load(std::string{ abilities });
        const std::expected<core::Abilities, core::Error> table = Read(root, warnings);
        if (!table) {
            return std::unexpected{ table.error() };
        }
        return RulesOf(root, warnings).transform([&](const core::CombatRules& rules) {
            return AbilitiesFile{ .abilities = *table, .combat = rules };
        });
    }
    catch (const YAML::Exception& error) {
        return std::unexpected{ internal::ErrorOf("abilities", error) };
    }
}

std::expected<core::Abilities, core::Error> ParseAbilities(std::string_view abilities, std::vector<std::string>* warnings) {
    try {
        return Read(YAML::Load(std::string{ abilities }), warnings);
    }
    catch (const YAML::Exception& error) {
        return std::unexpected{ internal::ErrorOf("abilities", error) };
    }
}

} // namespace data
