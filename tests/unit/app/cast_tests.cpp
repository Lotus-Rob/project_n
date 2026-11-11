#include "app/cast.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

#include <doctest/doctest.h>

namespace {

constexpr core::Vec2 HERE{ .x = 10.0F, .y = 20.0F };
constexpr core::Vec2 THERE{ .x = -30.0F, .y = 40.0F };

// Whether a press or release of `control` casts at `point` (none: the cursor isn't over the window). A call of its own:
// MSVC's C4866 refuses CastAction's argument and result temporaries inside a CHECK (tests/AGENTS.md).
bool Casts(app::Control control, bool pressed, bool combat, bool alt, std::optional<core::Vec2> point) {
    return app::CastAction(control, pressed, combat, alt, point).has_value();
}

// The cast a press of `control` in combat sends at `point`, or a default Action when it sends none.
core::Action PressedInCombat(app::Control control, core::Vec2 point) {
    return app::CastAction(control, true, true, false, point).value_or(core::Action{});
}

TEST_CASE("a left click in combat casts the generator at the point") {
    const bool casts1 = Casts(app::Control::Primary, true, true, false, HERE);
    REQUIRE(casts1);
    const core::Action cast = PressedInCombat(app::Control::Primary, HERE);
    CHECK(cast.kind == core::ActionKind::Cast);
    CHECK(cast.slot == core::Slot::Generator);
    CHECK(cast.point.x == HERE.x);
    CHECK(cast.point.y == HERE.y);
}

TEST_CASE("a right click in combat casts the spender") {
    const bool casts2 = Casts(app::Control::Secondary, true, true, false, HERE);
    REQUIRE(casts2);
    CHECK(PressedInCombat(app::Control::Secondary, HERE).slot == core::Slot::Spender);
}

TEST_CASE("two quick left clicks are two casts") {
    // Press, release, press in one frame's events: the release sends nothing and the presses aren't merged.
    struct Click {
        bool pressed = false;
        core::Vec2 point{};
    };
    constexpr std::array CLICKS{
        Click{ .pressed = true, .point = HERE },
        Click{ .pressed = false, .point = HERE },
        Click{ .pressed = true, .point = THERE },
    };
    std::vector<core::Action> sent;
    for (const Click& click : CLICKS) {
        const std::optional<core::Action> cast =
            app::CastAction(app::Control::Primary, click.pressed, true, false, click.point);
        if (cast) {
            sent.push_back(*cast);
        }
    }
    REQUIRE(sent.size() == 2);
    CHECK(sent[0].slot == core::Slot::Generator);
    CHECK(sent[1].slot == core::Slot::Generator);
    CHECK(sent[0].point.x == HERE.x);
    CHECK(sent[1].point.x == THERE.x);
}

TEST_CASE("a press in combat casts its slot at the point") {
    struct Pair {
        app::Control control;
        core::Slot slot;
    };
    constexpr std::array PAIRS{
        Pair{ .control = app::Control::Movement, .slot = core::Slot::Movement },
        Pair{ .control = app::Control::Special1, .slot = core::Slot::Special1 },
        Pair{ .control = app::Control::Special2, .slot = core::Slot::Special2 },
        Pair{ .control = app::Control::Special3, .slot = core::Slot::Special3 },
        Pair{ .control = app::Control::Special4, .slot = core::Slot::Special4 },
        Pair{ .control = app::Control::Ultimate, .slot = core::Slot::Ultimate },
    };
    for (const Pair& pair : PAIRS) {
        const bool casts3 = Casts(pair.control, true, true, false, THERE);
        REQUIRE(casts3);
        const core::Action cast = PressedInCombat(pair.control, THERE);
        CHECK(cast.slot == pair.slot);
        CHECK(cast.point.y == THERE.y);
    }
}

TEST_CASE("a release casts nothing") {
    const bool casts4 = Casts(app::Control::Primary, false, true, false, HERE);
    CHECK_FALSE(casts4);
    const bool casts5 = Casts(app::Control::Ultimate, false, true, false, HERE);
    CHECK_FALSE(casts5);
}

TEST_CASE("a held button casts once") {
    // The press is one event and the held button sends no more: only the press casts, the release nothing.
    const std::optional<core::Action> press = app::CastAction(app::Control::Secondary, true, true, false, HERE);
    const std::optional<core::Action> release = app::CastAction(app::Control::Secondary, false, true, false, HERE);
    CHECK(press.has_value());
    CHECK_FALSE(release.has_value());
}

TEST_CASE("Alt and a left click selects instead in combat") {
    CHECK(app::SelectsInstead(app::Control::Primary, true, true));
    CHECK_FALSE(app::SelectsInstead(app::Control::Primary, true, false));
    const bool casts6 = Casts(app::Control::Primary, true, true, true, HERE);
    CHECK_FALSE(casts6);
    const bool casts7 = Casts(app::Control::Secondary, true, true, true, HERE);
    CHECK(casts7);
}

TEST_CASE("outside combat a left click selects and the other slots cast nothing") {
    CHECK(app::SelectsInstead(app::Control::Primary, false, false));
    CHECK_FALSE(app::SelectsInstead(app::Control::Secondary, false, false));
    for (const app::Control control : { app::Control::Primary,
                                        app::Control::Secondary,
                                        app::Control::Movement,
                                        app::Control::Special1,
                                        app::Control::Ultimate }) {
        const bool casts8 = Casts(control, true, false, false, HERE);
        CHECK_FALSE(casts8);
    }
}

TEST_CASE("a key cast without a cursor sends nothing") {
    const bool casts9 = Casts(app::Control::Movement, true, true, false, std::nullopt);
    CHECK_FALSE(casts9);
    const bool casts10 = Casts(app::Control::Special2, true, true, false, std::nullopt);
    CHECK_FALSE(casts10);
}

TEST_CASE("a control without a slot casts nothing") {
    CHECK_FALSE(app::SlotOf(app::Control::Zoom).has_value());
    const bool casts11 = Casts(app::Control::Save, true, true, false, HERE);
    CHECK_FALSE(casts11);
}

constexpr u64 PRESSED_NS = 10'000'000;
constexpr u64 LATER_NS = 20'000'000;  // 10 ms after the press
constexpr u64 LATEST_NS = 45'000'000; // 35 ms after it

// A volley of `ability` cast as use `use`, as the snapshot reports one.
core::VolleyView VolleyOf(core::AbilityKind ability, u64 use) {
    return { .volley = { .use = use }, .ability = ability };
}

TEST_CASE("the latency runs from the press to the first frame drawing its volley") {
    constexpr u64 USE = 7;
    const std::array swords{ VolleyOf(core::AbilityKind::FlyingSwords, USE) };
    app::CastLatency latency{ core::AbilityKind::FlyingSwords };
    latency.Pressed(PRESSED_NS);
    const std::optional<u64> drawn1 = latency.Drawn({}, LATER_NS); // not drawn yet
    CHECK_FALSE(drawn1.has_value());
    const std::optional<u64> drawn2 = latency.Drawn(swords, LATEST_NS);
    CHECK(drawn2 == LATEST_NS - PRESSED_NS);
}

TEST_CASE("a volley drawn twice counts once") {
    constexpr u64 USE = 7;
    constexpr u64 PRESSED_AGAIN_NS = 50'000'000;
    constexpr u64 LAST_NS = 60'000'000;
    const std::array swords{ VolleyOf(core::AbilityKind::FlyingSwords, USE) };
    app::CastLatency latency{ core::AbilityKind::FlyingSwords };
    latency.Pressed(PRESSED_NS);
    const std::optional<u64> drawn3 = latency.Drawn(swords, LATER_NS);
    CHECK(drawn3 == LATER_NS - PRESSED_NS);
    const std::optional<u64> drawn4 = latency.Drawn(swords, LATEST_NS);
    CHECK_FALSE(drawn4.has_value());
    latency.Pressed(PRESSED_AGAIN_NS); // waits for a volley of its own: the one still drawn isn't it
    const std::optional<u64> drawn5 = latency.Drawn(swords, LAST_NS);
    CHECK_FALSE(drawn5.has_value());
}

TEST_CASE("a volley of another ability, or with no press waiting, gives no latency") {
    constexpr u64 BOLT_USE = 3;
    constexpr u64 SWORDS_USE = 4;
    constexpr u64 BEFORE_NS = 5'000'000;
    const std::array bolt{ VolleyOf(core::AbilityKind::FireBolt, BOLT_USE) };
    const std::array both{ VolleyOf(core::AbilityKind::FireBolt, BOLT_USE),
                           VolleyOf(core::AbilityKind::FlyingSwords, SWORDS_USE) };
    app::CastLatency latency{ core::AbilityKind::FlyingSwords };
    const std::optional<u64> drawn6 = latency.Drawn(both, BEFORE_NS); // nothing pressed
    CHECK_FALSE(drawn6.has_value());
    latency.Pressed(PRESSED_NS);
    const std::optional<u64> drawn7 = latency.Drawn(bolt, LATER_NS);
    CHECK_FALSE(drawn7.has_value());
    const std::array later{ VolleyOf(core::AbilityKind::FireBolt, BOLT_USE),
                            VolleyOf(core::AbilityKind::FlyingSwords, SWORDS_USE + 1) };
    const std::optional<u64> drawn8 = latency.Drawn(later, LATEST_NS);
    CHECK(drawn8 == LATEST_NS - PRESSED_NS);
}

TEST_CASE("a press no volley answers within the limit is forgotten") {
    constexpr u64 USE = 9;
    constexpr u64 WAITED_NS = 30'000'000;
    const std::array swords{ VolleyOf(core::AbilityKind::FlyingSwords, USE) };
    app::CastLatency latency{ core::AbilityKind::FlyingSwords };
    latency.Pressed(0); // refused, say: on cooldown
    const std::optional<u64> drawn9 = latency.Drawn({}, app::CAST_LATENCY_LIMIT_NS + 1);
    CHECK_FALSE(drawn9.has_value());
    const u64 pressed = app::CAST_LATENCY_LIMIT_NS + PRESSED_NS;
    latency.Pressed(pressed);
    const std::optional<u64> drawn10 = latency.Drawn(swords, pressed + WAITED_NS);
    CHECK(drawn10 == WAITED_NS);
}

constexpr i64 QI_MAX = 100;

// A caster with the shipped loadout, `held` Qi of QI_MAX and every slot ready at tick 0.
core::CasterView CasterWith(i64 held) {
    return { .qi = held,
             .qiMax = QI_MAX,
             .health = 1,
             .healthMax = 1,
             .readyTick = {},
             .loadout = { {
                 core::AbilityKind::FireBolt,
                 core::AbilityKind::PoisonLance,
                 core::AbilityKind::Dash,
                 core::AbilityKind::FlyingSwords,
                 core::AbilityKind::None,
                 core::AbilityKind::None,
                 core::AbilityKind::None,
                 core::AbilityKind::None,
             } } };
}

// Abilities whose swords cost SWORDS_COST.
constexpr i64 SWORDS_COST = 60;
core::Abilities Costed() {
    core::Abilities abilities;
    abilities.qiMax = QI_MAX;
    abilities.table.at(std::to_underlying(core::AbilityKind::FlyingSwords)).qiCost = SWORDS_COST;
    return abilities;
}

constexpr u64 NOW_TICK = 500;

TEST_CASE("the timed cast sends the swords at the point when they are ready and the Qi covers them") {
    const std::optional<core::Action> cast = app::TimedCast(CasterWith(SWORDS_COST), Costed(), NOW_TICK, HERE);
    REQUIRE(cast.has_value());
    CHECK(cast.value_or(core::Action{}).kind == core::ActionKind::Cast);
    CHECK(cast.value_or(core::Action{}).slot == core::Slot::Special1);
    CHECK(cast.value_or(core::Action{}).point.x == HERE.x);
}

TEST_CASE("short of Qi for the swords, the timed cast sends the generator") {
    const std::optional<core::Action> cast = app::TimedCast(CasterWith(SWORDS_COST - 1), Costed(), NOW_TICK, HERE);
    REQUIRE(cast.has_value());
    CHECK(cast.value_or(core::Action{}).slot == core::Slot::Generator);
    CHECK(cast.value_or(core::Action{}).point.y == HERE.y);
}

TEST_CASE("the timed cast waits while the slot it would cast cools down") {
    core::CasterView swordsCooling = CasterWith(SWORDS_COST);
    swordsCooling.readyTick.at(std::to_underlying(core::Slot::Special1)) = NOW_TICK + 1;
    CHECK_FALSE(app::TimedCast(swordsCooling, Costed(), NOW_TICK, HERE).has_value());
    core::CasterView generatorCooling = CasterWith(0);
    generatorCooling.readyTick.at(std::to_underlying(core::Slot::Generator)) = NOW_TICK + 1;
    CHECK_FALSE(app::TimedCast(generatorCooling, Costed(), NOW_TICK, HERE).has_value());
    swordsCooling.readyTick.at(std::to_underlying(core::Slot::Special1)) = NOW_TICK; // ready from this tick on
    CHECK(app::TimedCast(swordsCooling, Costed(), NOW_TICK, HERE).has_value());
}

TEST_CASE("the timed cast sends nothing without swords in the first special's slot") {
    core::CasterView empty = CasterWith(SWORDS_COST);
    empty.loadout.at(std::to_underlying(core::Slot::Special1)) = core::AbilityKind::None;
    CHECK_FALSE(app::TimedCast(empty, Costed(), NOW_TICK, HERE).has_value());
}

} // namespace
