#include "core/cxx23.hpp" // IWYU pragma: keep
#include "data/abilities.hpp"

#include <doctest/doctest.h>

namespace {

// What assets/data/abilities.yaml ships.
constexpr std::string_view ABILITIES = R"(qi: { max: 100, start: 50 }
abilities:
  fire_bolt:     { slot: generator, cooldown: 0.25, qi_gain: 10, damage: 140, element: burn, radius: 120, speed: 1500,
                   burn: { ticks: 3, damage: 40 } }
  poison_lance:  { slot: spender, cooldown: 0.5, qi_cost: 25, damage: 360, element: poison, radius: 90,
                   poison: { ticks: 4, damage: 60 } }
  dash:          { slot: movement, cooldown: 1.0, distance: 260, duration: 0.15 }
  flying_swords: { slot: special, cooldown: 6, qi_cost: 60, damage: 20, radius: 260,
                   swords: { count: 1000, waves: 10, wave_gap: 0.15, ring_radius: 160, ring_step: 38, scatter: 0,
                             angle_jitter: 0, stagger: 0.05, out: 0.12, hold: 0.3, depart_jitter: 0.25, flight: 1.7,
                             sweep: 420, sweep_jitter: 0.6, fade: 0.3 } }
loadout: { generator: fire_bolt, spender: poison_lance, movement: dash, special_1: flying_swords }
)";
// The line of the loadout in ABILITIES.
constexpr std::string_view LOADOUT_LINE = "line 12";

// ABILITIES with `from` replaced by `to`.
std::string With(std::string_view from, std::string_view replacement) {
    std::string text{ ABILITIES };
    const std::size_t where = text.find(from);
    REQUIRE(where != std::string::npos);
    return text.replace(where, from.size(), replacement);
}

core::Error ErrorFor(const std::string& text) {
    const std::expected<core::Abilities, core::Error> abilities = data::ParseAbilities(text);
    REQUIRE_FALSE(abilities.has_value());
    return abilities.error();
}

constexpr f32 BOLT_SPEED = 25.0F; // 1500 world units a second

TEST_CASE("the abilities parse") {
    const std::expected<core::Abilities, core::Error> abilities = data::ParseAbilities(ABILITIES);
    REQUIRE(abilities.has_value());
    CHECK(abilities->qiMax == 100);
    CHECK(abilities->qiStart == 50);
    CHECK(abilities->In(core::Slot::Special1) == core::AbilityKind::FlyingSwords);
    CHECK(abilities->In(core::Slot::Special2) == core::AbilityKind::None);
    CHECK(abilities->Of(core::AbilityKind::FlyingSwords).volley.count == 1'000);
    CHECK(abilities->Of(core::AbilityKind::FireBolt).hit == core::HitKind::Burn);
    CHECK(abilities->Of(core::AbilityKind::PoisonLance).hit == core::HitKind::Poison);
    CHECK(abilities->Of(core::AbilityKind::Dash).slot == core::SlotKind::Movement);
    CHECK(abilities->Of(core::AbilityKind::FireBolt).cooldownTicks == 15); // 0.25 s
    CHECK(abilities->Of(core::AbilityKind::FireBolt).affliction.ticks == 3);
    CHECK(abilities->Of(core::AbilityKind::FireBolt).affliction.damage == 40);
    CHECK(abilities->Of(core::AbilityKind::FireBolt).speed == doctest::Approx(BOLT_SPEED));
    CHECK(abilities->Of(core::AbilityKind::Dash).durationTicks == 9);
    CHECK(abilities->Of(core::AbilityKind::FlyingSwords).volley.flightTicks == 102); // 1.7 s
    CHECK(abilities->Valid());
}

TEST_CASE("the swords' waves, rings, hold and jitters parse") {
    constexpr f32 RING_STEP = 38.0F;
    constexpr f32 SWEEP_JITTER = 0.6F;
    const std::expected<core::Abilities, core::Error> abilities = data::ParseAbilities(ABILITIES);
    REQUIRE(abilities.has_value());
    const core::VolleyShape& swords = abilities->Of(core::AbilityKind::FlyingSwords).volley;
    CHECK(swords.waves == 10);
    CHECK(swords.waveGapTicks == 9);       // 0.15 s
    CHECK(swords.staggerTicks == 3);       // 0.05 s, within a wave
    CHECK(swords.outTicks == 7);           // 0.12 s, rounded
    CHECK(swords.holdTicks == 18);         // 0.3 s
    CHECK(swords.departJitterTicks == 15); // 0.25 s
    CHECK(swords.ringStep == RING_STEP);
    CHECK(swords.scatter == 0.0F); // even rings
    CHECK(swords.angleJitter == 0.0F);
    CHECK(swords.sweepJitter == SWEEP_JITTER);
}

TEST_CASE("the swords' waves, rings, hold and jitters are refused out of range or missing, naming the key") {
    struct Broken {
        std::string_view from;
        std::string_view replacement;
        std::string_view says;
    };
    constexpr std::array<Broken, 20> CASES{ {
        Broken{ .from = "waves: 10", .replacement = "waves: 0", .says = "flying_swords's swords's waves" },
        Broken{ .from = "waves: 10", .replacement = "waves: -1", .says = "flying_swords's swords's waves" },
        Broken{ .from = "waves: 10, ", .replacement = "", .says = "flying_swords's swords has no waves" },
        Broken{ .from = "wave_gap: 0.15", .replacement = "wave_gap: -0.1", .says = "flying_swords's swords's wave_gap" },
        Broken{ .from = "wave_gap: 0.15, ", .replacement = "", .says = "flying_swords's swords has no wave_gap" },
        Broken{ .from = "ring_step: 38", .replacement = "ring_step: .inf", .says = "flying_swords's swords's ring_step" },
        Broken{ .from = "ring_step: 38, ", .replacement = "", .says = "flying_swords's swords has no ring_step" },
        Broken{ .from = "scatter: 0,", .replacement = "scatter: 1,", .says = "flying_swords's swords's scatter" },
        Broken{ .from = "scatter: 0,", .replacement = "scatter: .nan,", .says = "flying_swords's swords's scatter" },
        Broken{ .from = "scatter: 0,", .replacement = "scatter: -0.1,", .says = "flying_swords's swords's scatter" },
        Broken{ .from = "scatter: 0,", .replacement = "", .says = "flying_swords's swords has no scatter" },
        Broken{
            .from = "angle_jitter: 0,", .replacement = "angle_jitter: -0.5,", .says = "flying_swords's swords's angle_jitter" },
        Broken{ .from = "angle_jitter: 0, ", .replacement = "", .says = "flying_swords's swords has no angle_jitter" },
        Broken{ .from = "hold: 0.3", .replacement = "hold: -0.3", .says = "flying_swords's swords's hold" },
        Broken{ .from = "hold: 0.3, ", .replacement = "", .says = "flying_swords's swords has no hold" },
        Broken{ .from = "depart_jitter: 0.25",
                .replacement = "depart_jitter: -0.25",
                .says = "flying_swords's swords's depart_jitter" },
        Broken{ .from = "depart_jitter: 0.25, ", .replacement = "", .says = "flying_swords's swords has no depart_jitter" },
        Broken{
            .from = "sweep_jitter: 0.6", .replacement = "sweep_jitter: 1.5", .says = "flying_swords's swords's sweep_jitter" },
        Broken{
            .from = "sweep_jitter: 0.6", .replacement = "sweep_jitter: -0.2", .says = "flying_swords's swords's sweep_jitter" },
        Broken{ .from = "sweep_jitter: 0.6,", .replacement = "", .says = "flying_swords's swords has no sweep_jitter" },
    } };
    for (const Broken& broken : CASES) {
        CAPTURE(broken.from);
        CAPTURE(broken.replacement);
        const core::Error error = ErrorFor(With(broken.from, broken.replacement));
        CHECK(error.message.contains(broken.says));
        CHECK(error.message.contains("abilities line"));
    }
}

TEST_CASE("an ability in a slot of another kind is refused") {
    const core::Error error = ErrorFor(With("spender: poison_lance", "spender: dash"));
    CHECK(error.message.contains(LOADOUT_LINE));
    CHECK(error.message.contains("spender"));
    CHECK(error.message.contains("dash"));
}

TEST_CASE("an unknown ability or slot is refused, naming the line") {
    const core::Error ability = ErrorFor(With("generator: fire_bolt", "generator: ice_bolt"));
    CHECK(ability.message.contains(LOADOUT_LINE));
    CHECK(ability.message.contains("ice_bolt"));
    const core::Error slot = ErrorFor(With("special_1: flying_swords", "special_9: flying_swords"));
    CHECK(slot.message.contains(LOADOUT_LINE));
    CHECK(slot.message.contains("special_9"));
    const core::Error unknown = ErrorFor(With("  dash:", "  leap:"));
    CHECK(unknown.message.contains("leap isn't an ability"));
    const core::Error kind = ErrorFor(With("slot: movement", "slot: flying"));
    CHECK(kind.message.contains("flying"));
}

TEST_CASE("a negative, non-finite or missing number is refused, naming the ability and the key") {
    struct Broken {
        std::string_view from;
        std::string_view replacement;
        std::string_view says;
    };
    constexpr std::array<Broken, 6> CASES{ {
        Broken{ .from = "damage: 140", .replacement = "damage: -1", .says = "fire_bolt's damage" },
        Broken{ .from = "radius: 90", .replacement = "radius: .inf", .says = "poison_lance's radius" },
        Broken{ .from = "cooldown: 1.0", .replacement = "cooldown: .nan", .says = "dash's cooldown" },
        Broken{ .from = "cooldown: 6, ", .replacement = "", .says = "flying_swords has no cooldown" },
        Broken{ .from = "count: 1000", .replacement = "count: lots", .says = "flying_swords's swords's count" },
        Broken{ .from = "max: 100", .replacement = "max: -5", .says = "qi's max" },
    } };
    for (const Broken& broken : CASES) {
        CAPTURE(broken.says);
        const core::Error error = ErrorFor(With(broken.from, broken.replacement));
        CHECK(error.message.contains(broken.says));
        CHECK(error.message.contains("abilities line"));
    }
}

TEST_CASE("a time over a million seconds is refused, naming the ability and the key") {
    const core::Error cooldown = ErrorFor(With("cooldown: 1.0", "cooldown: 1000001"));
    CHECK(cooldown.message.contains("dash's cooldown isn't a number from 0 to 1e+06"));
    const core::Error flight = ErrorFor(With("flight: 1.7", "flight: 1e7"));
    CHECK(flight.message.contains("flying_swords's swords's flight isn't a number from 0 to 1e+06"));
}

TEST_CASE("a loadout entry for an ability the abilities don't define is refused") {
    const core::Error error =
        ErrorFor(With("  dash:          { slot: movement, cooldown: 1.0, distance: 260, duration: 0.15 }\n", ""));
    CHECK(error.message.contains("line 11")); // the loadout's, one up
    CHECK(error.message.contains("dash in the loadout isn't defined"));
}

TEST_CASE("an ability with both burn and poison is refused") {
    const core::Error error =
        ErrorFor(With("burn: { ticks: 3, damage: 40 }", "burn: { ticks: 3, damage: 40 }, poison: { ticks: 1, damage: 1 }"));
    CHECK(error.message.contains("fire_bolt has both burn and poison"));
}

TEST_CASE("a starting Qi above the maximum is refused") {
    const core::Error error = ErrorFor(With("start: 50", "start: 150"));
    CHECK(error.message.contains("line 1"));
    CHECK(error.message.contains("qi's start is more than its max"));
}

TEST_CASE("a dash without a distance or a duration is refused") {
    constexpr std::string_view DASH = "dash:          { slot: movement, cooldown: 1.0, distance: 260, duration: 0.15 }";
    for (const std::string_view broken : { "dash:          { slot: movement, cooldown: 1.0, distance: 0, duration: 0.15 }",
                                           "dash:          { slot: movement, cooldown: 1.0, distance: 260 }",
                                           "dash:          { slot: movement, cooldown: 1.0 }" }) {
        CAPTURE(broken);
        const core::Error error = ErrorFor(With(DASH, broken));
        CHECK(error.message.contains("line 7"));
        CHECK(error.message.contains("dash is a movement ability and needs a distance and a duration"));
    }
}

TEST_CASE("a volley of more swords than the limit is refused, naming the limit") {
    constexpr std::string_view LIMIT = "2000";
    const core::Error error = ErrorFor(With("count: 1000", "count: 2001"));
    CHECK(error.message.contains("line 9"));
    CHECK(error.message.contains("flying_swords's swords's count"));
    CHECK(error.message.contains(LIMIT));
    const std::expected<core::Abilities, core::Error> most = data::ParseAbilities(With("count: 1000", "count: 2000"));
    CHECK(most.has_value());
}

TEST_CASE("an affliction of more ticks than the limit is refused, naming the limit") {
    constexpr std::string_view LIMIT = "1000000";
    const std::expected<core::Abilities, core::Error> most = data::ParseAbilities(With("ticks: 3", "ticks: 1000000"));
    REQUIRE(most.has_value());
    CHECK(most->Of(core::AbilityKind::FireBolt).affliction.ticks == 1'000'000);
    const core::Error error = ErrorFor(With("ticks: 3", "ticks: 1000001"));
    CHECK(error.message.contains("line 4"));
    CHECK(error.message.contains("fire_bolt's burn's ticks"));
    CHECK(error.message.contains(LIMIT));
}

TEST_CASE("a volley shape above the limit is not valid") {
    core::VolleyShape shape{ .count = core::MAX_VOLLEY_COUNT };
    CHECK(shape.Valid());
    shape.count = core::MAX_VOLLEY_COUNT + 1;
    CHECK_FALSE(shape.Valid());
}

TEST_CASE("an unknown key is a warning with its line, and the rest still parse") {
    std::vector<std::string> warnings;
    const std::expected<core::Abilities, core::Error> abilities =
        data::ParseAbilities(With("cooldown: 0.5,", "cooldown: 0.5, colour: red,"), &warnings);
    REQUIRE(abilities.has_value());
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "abilities line 5: poison_lance has no key colour; it is ignored");
    CHECK(abilities->Of(core::AbilityKind::PoisonLance).cooldownTicks == 30);
}

TEST_CASE("a loadout may leave slots empty") {
    const std::expected<core::Abilities, core::Error> abilities =
        data::ParseAbilities(With("movement: dash, special_1: flying_swords", "special_1: flying_swords"));
    REQUIRE(abilities.has_value());
    CHECK(abilities->In(core::Slot::Movement) == core::AbilityKind::None);
    CHECK(abilities->In(core::Slot::Special2) == core::AbilityKind::None);
}

TEST_CASE("a non-map or a broken file is refused with a reason") {
    CHECK(ErrorFor("- qi\n").message.contains("not a map"));
    CHECK_FALSE(ErrorFor("qi: [").message.empty());
}

// The swords' line in ABILITIES, ending where a motion block can follow.
constexpr std::string_view SWORDS_END = "sweep: 420, sweep_jitter: 0.6, fade: 0.3 } }";
// The shipped motion, spelled out as abilities.yaml has it (retarget_speed in world units a second).
constexpr std::string_view SHIPPED_MOTION =
    "aim_scatter: 18, short_bend: 0.5, turn: 0.1333, arrival_speed: 0.5, retarget: 0.1333, retarget_speed: 960";

// ABILITIES with the swords given a motion block of `motion`.
std::string WithMotion(std::string_view motion) {
    return With(SWORDS_END, std::format("sweep: 420, sweep_jitter: 0.6, fade: 0.3 }}, motion: {{ {} }} }}", motion));
}

TEST_CASE("a motion block spelling out the shipped motion reads the shape's defaults") {
    const std::expected<core::Abilities, core::Error> abilities = data::ParseAbilities(WithMotion(SHIPPED_MOTION));
    REQUIRE(abilities.has_value());
    const core::VolleyShape& swords = abilities->Of(core::AbilityKind::FlyingSwords).volley;
    constexpr core::VolleyShape SHIPPED{};
    CHECK(swords.aimScatter == SHIPPED.aimScatter);
    CHECK(swords.shortBend == SHIPPED.shortBend);
    CHECK(swords.turnTicks == SHIPPED.turnTicks);
    CHECK(swords.arrivalSpeed == SHIPPED.arrivalSpeed);
    CHECK(swords.retargetTicks == SHIPPED.retargetTicks);
    CHECK(swords.retargetSpeed == SHIPPED.retargetSpeed);
    CHECK(abilities->Valid());
}

TEST_CASE("a motion block sets the swords' motion in ticks and units a tick, and a key left out keeps its default") {
    const std::expected<core::Abilities, core::Error> abilities =
        data::ParseAbilities(WithMotion("aim_scatter: 30, turn: 0.25, arrival_speed: 1.5, retarget_speed: 1200"));
    REQUIRE(abilities.has_value());
    const core::VolleyShape& swords = abilities->Of(core::AbilityKind::FlyingSwords).volley;
    constexpr f32 SCATTER = 30.0F;
    constexpr u32 QUARTER_SECOND = 15;
    constexpr f32 ARRIVAL = 1.5F;
    constexpr f32 PER_TICK = 20.0F; // 1200 units a second
    CHECK(swords.aimScatter == SCATTER);
    CHECK(swords.turnTicks == QUARTER_SECOND);
    CHECK(swords.arrivalSpeed == ARRIVAL);
    CHECK(swords.retargetSpeed == PER_TICK);
    CHECK(swords.shortBend == core::VolleyShape{}.shortBend);
    CHECK(swords.retargetTicks == core::VolleyShape{}.retargetTicks);
}

TEST_CASE("the bolt takes a motion block too") {
    const std::expected<core::Abilities, core::Error> abilities = data::ParseAbilities(
        With("burn: { ticks: 3, damage: 40 } }", "burn: { ticks: 3, damage: 40 }, motion: { short_bend: 0.25 } }")
    );
    REQUIRE(abilities.has_value());
    constexpr f32 QUARTER = 0.25F;
    CHECK(abilities->Of(core::AbilityKind::FireBolt).volley.shortBend == QUARTER);
}

TEST_CASE("a motion value out of range is refused, naming the key") {
    struct Broken {
        std::string_view motion;
        std::string_view says;
    };
    constexpr std::array<Broken, 8> CASES{ {
        Broken{ .motion = "aim_scatter: -1", .says = "flying_swords's motion's aim_scatter" },
        Broken{ .motion = "short_bend: 0", .says = "flying_swords's motion's short_bend" },
        Broken{ .motion = "turn: -0.1", .says = "flying_swords's motion's turn" },
        Broken{ .motion = "arrival_speed: 3.5", .says = "flying_swords's motion's arrival_speed" },
        Broken{ .motion = "retarget: 0", .says = "flying_swords's motion's retarget is under 1 tick" },
        Broken{ .motion = "retarget_speed: 0", .says = "flying_swords's motion's retarget_speed" },
        Broken{ .motion = "retarget_speed: .inf", .says = "flying_swords's motion's retarget_speed" },
        Broken{ .motion = "aim_scatter: wide", .says = "flying_swords's motion's aim_scatter isn't a number" },
    } };
    for (const Broken& broken : CASES) {
        CAPTURE(broken.motion);
        const core::Error error = ErrorFor(WithMotion(broken.motion));
        CHECK(error.message.contains(broken.says));
        CHECK(error.message.contains("abilities line"));
    }
}

TEST_CASE("a motion block on an ability that flies nothing is refused with its line") {
    const core::Error error = ErrorFor(With("duration: 0.15 }", "duration: 0.15, motion: { aim_scatter: 5 } }"));
    CHECK(error.message.contains("dash has a motion but flies nothing"));
    CHECK(error.message.contains("abilities line"));
}

TEST_CASE("an unknown motion key is a warning with its line") {
    std::vector<std::string> warnings;
    const std::expected<core::Abilities, core::Error> abilities = data::ParseAbilities(WithMotion("wobble: 3"), &warnings);
    REQUIRE(abilities.has_value());
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].contains("flying_swords's motion has no key wobble"));
}

TEST_CASE("a volley shape with a motion out of range is not valid") {
    core::VolleyShape shape{};
    CHECK(shape.Valid());
    shape.retargetSpeed = 0.0F;
    CHECK_FALSE(shape.Valid());
    shape = {};
    shape.arrivalSpeed = core::MAX_ARRIVAL_SPEED + 1.0F;
    CHECK_FALSE(shape.Valid());
    shape = {};
    shape.retargetTicks = 0;
    CHECK_FALSE(shape.Valid());
}

// ABILITIES with a combat section of `combat` after the loadout.
std::string WithCombat(std::string_view combat) {
    return std::format("{}combat:\n{}\n", ABILITIES, combat);
}

core::Error CombatErrorFor(const std::string& text) {
    const std::expected<core::CombatRules, core::Error> rules = data::ParseCombatRules(text);
    REQUIRE_FALSE(rules.has_value());
    return rules.error();
}

TEST_CASE("a file without a combat section has the shipped rules") {
    const std::expected<core::CombatRules, core::Error> rules = data::ParseCombatRules(ABILITIES);
    REQUIRE(rules.has_value());
    CHECK(rules->dyingTicks == core::CombatRules{}.dyingTicks);
    CHECK(rules->Valid());
}

TEST_CASE("the combat rules read seconds as ticks, and the aura's reach and ranges") {
    const std::expected<core::CombatRules, core::Error> rules = data::ParseCombatRules(WithCombat(
        "  dying: 1\n  respawn: 0\n  arrive: 2\n  affliction_period: 0.5\n  flash: 0.25\n"
        "  aura: { period: 2, reach: 100, damage_min: 1, damage_max: 2, healing_min: 3, healing_max: 4 }"
    ));
    REQUIRE(rules.has_value());
    CHECK(rules->dyingTicks == 60);
    CHECK(rules->respawnTicks == 0);
    CHECK(rules->arriveTicks == 120);
    CHECK(rules->afflictionPeriod == 30);
    CHECK(rules->flashTicks == 15);
    CHECK(rules->auraPeriod == 120);
    CHECK(rules->auraReach == 100.0F);
    CHECK(rules->auraDamageMin == 1);
    CHECK(rules->auraDamageMax == 2);
    CHECK(rules->auraHealingMin == 3);
    CHECK(rules->auraHealingMax == 4);
    CHECK(rules->Valid());
}

TEST_CASE("a combat value out of range is refused, naming the key") {
    struct Broken {
        std::string_view combat;
        std::string_view says;
    };
    constexpr std::array<Broken, 8> CASES{ {
        Broken{ .combat = "  dying: 0", .says = "combat's dying is under 1 tick" },
        Broken{ .combat = "  respawn: -1", .says = "combat's respawn" },
        Broken{ .combat = "  arrive: 0", .says = "combat's arrive is under 1 tick" },
        Broken{ .combat = "  affliction_period: 0", .says = "combat's affliction_period is under 1 tick" },
        Broken{ .combat = "  flash: 0", .says = "combat's flash is under 1 tick" },
        Broken{ .combat = "  aura: { period: 0 }", .says = "combat's aura's period is under 1 tick" },
        Broken{ .combat = "  aura: { reach: -5 }", .says = "combat's aura's reach" },
        Broken{ .combat = "  aura: { damage_min: 500 }", .says = "damage_min above its damage_max" },
    } };
    for (const Broken& broken : CASES) {
        CAPTURE(broken.combat);
        const core::Error error = CombatErrorFor(WithCombat(broken.combat));
        CHECK(error.message.contains(broken.says));
        CHECK(error.message.contains("abilities line"));
    }
}

TEST_CASE("an unknown combat key is a warning with its line") {
    std::vector<std::string> warnings;
    const std::expected<core::CombatRules, core::Error> rules =
        data::ParseCombatRules(WithCombat("  dying: 1\n  mourning: 3"), &warnings);
    REQUIRE(rules.has_value());
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].contains("combat has no key mourning"));
}

TEST_CASE("the abilities reader leaves the combat section to the combat reader, without a warning") {
    std::vector<std::string> warnings;
    const std::expected<core::Abilities, core::Error> abilities = data::ParseAbilities(WithCombat("  dying: 1"), &warnings);
    REQUIRE(abilities.has_value());
    CHECK(warnings.empty());
}

} // namespace
