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
                   swords: { count: 1000, waves: 5, wave_gap: 0.2, ring_radius: 160, ring_step: 100, scatter: 0.25,
                             angle_jitter: 0.5, stagger: 0.15, out: 0.25, flight: 1.1, sweep: 420, sweep_jitter: 0.6,
                             fade: 0.3 } }
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
    CHECK(abilities->Of(core::AbilityKind::FlyingSwords).volley.flightTicks == 66);
    CHECK(abilities->Valid());
}

TEST_CASE("the swords' waves, rings and jitters parse") {
    constexpr f32 RING_STEP = 100.0F;
    constexpr f32 SCATTER = 0.25F;
    constexpr f32 ANGLE_JITTER = 0.5F;
    constexpr f32 SWEEP_JITTER = 0.6F;
    const std::expected<core::Abilities, core::Error> abilities = data::ParseAbilities(ABILITIES);
    REQUIRE(abilities.has_value());
    const core::VolleyShape& swords = abilities->Of(core::AbilityKind::FlyingSwords).volley;
    CHECK(swords.waves == 5);
    CHECK(swords.waveGapTicks == 12); // 0.2 s
    CHECK(swords.staggerTicks == 9);  // 0.15 s, within a wave
    CHECK(swords.ringStep == RING_STEP);
    CHECK(swords.scatter == SCATTER);
    CHECK(swords.angleJitter == ANGLE_JITTER);
    CHECK(swords.sweepJitter == SWEEP_JITTER);
}

TEST_CASE("the swords' waves, rings and jitters are refused out of range or missing, naming the key") {
    struct Broken {
        std::string_view from;
        std::string_view replacement;
        std::string_view says;
    };
    constexpr std::array<Broken, 13> CASES{ {
        Broken{ .from = "waves: 5", .replacement = "waves: 0", .says = "flying_swords's swords's waves" },
        Broken{ .from = "waves: 5", .replacement = "waves: -1", .says = "flying_swords's swords's waves" },
        Broken{ .from = "waves: 5, ", .replacement = "", .says = "flying_swords's swords has no waves" },
        Broken{ .from = "wave_gap: 0.2", .replacement = "wave_gap: -0.1", .says = "flying_swords's swords's wave_gap" },
        Broken{ .from = "wave_gap: 0.2, ", .replacement = "", .says = "flying_swords's swords has no wave_gap" },
        Broken{ .from = "ring_step: 100", .replacement = "ring_step: .inf", .says = "flying_swords's swords's ring_step" },
        Broken{ .from = "ring_step: 100, ", .replacement = "", .says = "flying_swords's swords has no ring_step" },
        Broken{ .from = "scatter: 0.25", .replacement = "scatter: 1", .says = "flying_swords's swords's scatter" },
        Broken{ .from = "scatter: 0.25", .replacement = "scatter: .nan", .says = "flying_swords's swords's scatter" },
        Broken{
            .from = "angle_jitter: 0.5", .replacement = "angle_jitter: -0.5", .says = "flying_swords's swords's angle_jitter" },
        Broken{ .from = "angle_jitter: 0.5, ", .replacement = "", .says = "flying_swords's swords has no angle_jitter" },
        Broken{
            .from = "sweep_jitter: 0.6", .replacement = "sweep_jitter: 1.5", .says = "flying_swords's swords's sweep_jitter" },
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

} // namespace
