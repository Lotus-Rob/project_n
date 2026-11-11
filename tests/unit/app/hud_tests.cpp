#include "app/hud.hpp"
#include "core/abilities.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/strings.hpp"
#include "core/ui_colours.hpp"
#include "gfx/hud_draw.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 HEIGHT = 720;
constexpr f64 NOW = 1'000.0; // the render tick the HUD is built at
constexpr core::CasterView CASTER{
    .qi = 50,
    .qiMax = 100,
    .health = 300,
    .healthMax = 400,
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
    } },
};
constexpr std::size_t HEALTH_LINE = 0;
constexpr std::size_t QI_LINE = 1;
constexpr std::size_t FIRST_SLOT_LINE = 2;
constexpr std::size_t SPENDER_LINE = FIRST_SLOT_LINE + std::to_underlying(core::Slot::Spender);
constexpr std::size_t HEALTH_BAR = 0;
constexpr std::size_t QI_BAR = 1;

// The HUD's strings in both languages, as strings.yaml has them; every other string empty.
std::shared_ptr<const core::Strings> HudStrings() {
    auto strings = std::make_shared<core::Strings>();
    const auto set = [&strings](core::StringId string, std::string english, std::string chinese) {
        strings->text.at(std::to_underlying(string)) = { { std::move(english), std::move(chinese) } };
    };
    set(core::StringId::Health, "Health {value}/{max}", "生命 {value}/{max}");
    set(core::StringId::Qi, "Qi {value}/{max}", "气 {value}/{max}");
    set(core::StringId::NotEnoughQi, "Not enough Qi", "气不足");
    set(core::StringId::Cooldown, "{value}s", "{value}秒");
    set(core::StringId::SlotEmpty, "—", "—");
    set(core::StringId::KeyLeftClick, "Left click", "左键");
    set(core::StringId::KeyRightClick, "Right click", "右键");
    set(core::StringId::KeySpace, "Space", "空格");
    set(core::StringId::FireBolt, "Fire Bolt", "火弹");
    set(core::StringId::PoisonLance, "Poison Lance", "毒枪");
    set(core::StringId::Dash, "Dash", "疾步");
    set(core::StringId::FlyingSwords, "Flying Swords", "飞剑");
    return strings;
}

struct Built {
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<gfx::HudLine> lines{};
    std::vector<gfx::HudBar> bars{};
    // NOLINTEND(readability-redundant-member-init)
};

Built Build(app::Hud& hud, const core::CasterView& caster = CASTER, f64 renderTick = NOW) {
    Built built;
    hud.Build(caster, renderTick, HEIGHT, built.lines, built.bars);
    return built;
}

app::Hud English(const core::UiColours& colours = {}) {
    return app::Hud(HudStrings(), { .language = core::Language::English }, colours);
}

// CASTER with its spender cooling down for a few seconds more.
core::CasterView SpenderCooling() {
    constexpr u64 SECONDS = 180;
    core::CasterView cooling = CASTER;
    cooling.readyTick.at(std::to_underlying(core::Slot::Spender)) = static_cast<u64>(NOW) + SECONDS;
    return cooling;
}

// The texts of `lines`, copied out.
std::vector<std::string> Texts(const std::vector<gfx::HudLine>& lines) {
    std::vector<std::string> texts;
    texts.reserve(lines.size());
    for (const gfx::HudLine& line : lines) {
        texts.emplace_back(line.text);
    }
    return texts;
}

TEST_CASE("the HUD shows the health bar, the Qi bar and every slot in order") {
    app::Hud hud = English();
    const Built built = Build(hud);
    const std::vector<std::string> expected{
        "Health 300/400",
        "Qi 50/100",
        "Left click  Fire Bolt",
        "Right click  Poison Lance",
        "Space  Dash",
        "1  Flying Swords",
        "2  —",
        "3  —",
        "4  —",
        "5  —",
    };
    CHECK(Texts(built.lines) == expected);
    REQUIRE(built.bars.size() == 2);
    const gfx::HudBar& health = built.bars.at(HEALTH_BAR);
    const gfx::HudBar& qiBar = built.bars.at(QI_BAR);
    for (const gfx::HudBar& bar : built.bars) {
        CHECK(bar.topLeft.x == app::HUD_MARGIN);
        CHECK(bar.size.x == app::HUD_BAR_WIDTH);
        CHECK(bar.size.y == app::HUD_BAR_HEIGHT);
    }
    // Top to bottom: the health bar, the Qi bar, then the slots, the last 16 pixels over the bottom.
    CHECK(health.topLeft.y + health.size.y < qiBar.topLeft.y);
    CHECK(qiBar.topLeft.y + qiBar.size.y <= built.lines.at(FIRST_SLOT_LINE).topLeft.y);
    for (std::size_t line = FIRST_SLOT_LINE; line < built.lines.size(); ++line) {
        CAPTURE(line);
        CHECK(built.lines.at(line).topLeft.x == app::HUD_MARGIN);
        if (line > FIRST_SLOT_LINE) {
            CHECK(built.lines.at(line).topLeft.y == built.lines.at(line - 1).topLeft.y + app::HUD_LINE_HEIGHT);
        }
    }
    CHECK(built.lines.back().topLeft.y + app::HUD_LINE_HEIGHT == static_cast<f32>(HEIGHT) - app::HUD_MARGIN);
    // Each bar's words are written on it.
    for (const auto& [line, bar] : { std::pair{ HEALTH_LINE, health }, std::pair{ QI_LINE, qiBar } }) {
        const gfx::HudLine& text = built.lines.at(line);
        CHECK(text.topLeft.x > bar.topLeft.x);
        CHECK(text.topLeft.x < bar.topLeft.x + bar.size.x);
        CHECK(text.topLeft.y < bar.topLeft.y + bar.size.y);
        CHECK(text.topLeft.y + app::HUD_LINE_HEIGHT > bar.topLeft.y);
    }
}

TEST_CASE("each bar's fill is as long as its value, with the value written on it") {
    constexpr core::CasterView WOUNDED{ .qi = 25, .qiMax = 100, .health = 0, .healthMax = 400 };
    constexpr core::CasterView OVERFULL{ .qi = 150, .qiMax = 100, .health = 400, .healthMax = 400 };
    constexpr core::CasterView EMPTY{ .qi = 0, .qiMax = 0, .health = 0, .healthMax = 0 };
    constexpr f32 QUARTER = 0.25F;
    app::Hud hud = English();
    const Built wounded = Build(hud, WOUNDED);
    CHECK(wounded.bars.at(HEALTH_BAR).fraction == 0.0F);
    CHECK(wounded.bars.at(QI_BAR).fraction == QUARTER);
    CHECK(wounded.lines.at(HEALTH_LINE).text == "Health 0/400");
    CHECK(wounded.lines.at(QI_LINE).text == "Qi 25/100");
    const Built overfull = Build(hud, OVERFULL);
    CHECK(overfull.bars.at(HEALTH_BAR).fraction == 1.0F);
    CHECK(overfull.bars.at(QI_BAR).fraction == 1.0F);
    CHECK(overfull.lines.at(QI_LINE).text == "Qi 150/100");
    const Built empty = Build(hud, EMPTY); // no most: nothing to fill, and no division by it
    CHECK(empty.bars.at(HEALTH_BAR).fraction == 0.0F);
    CHECK(empty.bars.at(QI_BAR).fraction == 0.0F);
}

TEST_CASE("a slot cooling down shows its seconds left, rounded up") {
    constexpr core::UiColours COLOURS{};
    constexpr u64 TWO_SECONDS_LESS_A_TICK = 119;
    constexpr u64 ONE_SECOND = 60;
    app::Hud hud = English(COLOURS);
    core::CasterView cooling = CASTER;
    cooling.readyTick.at(std::to_underlying(core::Slot::Spender)) = static_cast<u64>(NOW) + TWO_SECONDS_LESS_A_TICK;
    const Built built = Build(hud, cooling);
    CHECK(built.lines.at(SPENDER_LINE).text == "Right click  Poison Lance  2s");
    CHECK(built.lines.at(SPENDER_LINE).colour == COLOURS.Of(core::UiColour::HudCooling));
    CHECK(built.lines.at(SPENDER_LINE - 1).colour == COLOURS.Of(core::UiColour::HudText));

    const Built second = Build(hud, cooling, NOW + ONE_SECOND - 1.0 + 0.5);
    CHECK(second.lines.at(SPENDER_LINE).text == "Right click  Poison Lance  1s");
    // Ready at its tick: no longer cooling.
    const Built ready = Build(hud, cooling, NOW + static_cast<f64>(TWO_SECONDS_LESS_A_TICK));
    CHECK(ready.lines.at(SPENDER_LINE).text == "Right click  Poison Lance");
    CHECK(ready.lines.at(SPENDER_LINE).colour == COLOURS.Of(core::UiColour::HudText));
}

TEST_CASE("too little Qi shows the words for a second") {
    constexpr core::UiColours COLOURS{};
    constexpr f64 SECOND = 60.0;
    constexpr f64 JUST_BEFORE = 0.01;
    app::Hud hud = English(COLOURS);
    const core::Event refused{ .tick = static_cast<u64>(NOW),
                               .kind = core::EventKind::NotEnoughQi,
                               .value = std::to_underlying(core::Slot::Special1) };
    hud.Play(refused, NOW);
    const Built warned = Build(hud, CASTER, NOW + SECOND - JUST_BEFORE);
    CHECK(warned.lines.at(QI_LINE).text == "Not enough Qi");
    CHECK(warned.lines.at(QI_LINE).colour == COLOURS.Of(core::UiColour::HudWarning));
    const Built after = Build(hud, CASTER, NOW + SECOND);
    CHECK(after.lines.at(QI_LINE).text == "Qi 50/100");
    CHECK(after.lines.at(QI_LINE).colour == COLOURS.Of(core::UiColour::HudText));

    // A new timeline forgets the words; other events say nothing.
    hud.Play(refused, NOW);
    hud.Clear();
    CHECK(Build(hud).lines.at(QI_LINE).text == "Qi 50/100");
    hud.Play({ .tick = static_cast<u64>(NOW), .kind = core::EventKind::Hit }, NOW);
    hud.Play({ .tick = static_cast<u64>(NOW), .kind = core::EventKind::UseEnded }, NOW);
    CHECK(Build(hud).lines.at(QI_LINE).text == "Qi 50/100");
}

TEST_CASE("the HUD reads in Chinese with the language set") {
    constexpr u64 TWO_SECONDS = 120;
    app::Hud hud(HudStrings(), { .language = core::Language::Chinese }, core::UiColours{});
    core::CasterView cooling = CASTER;
    cooling.readyTick.at(std::to_underlying(core::Slot::Generator)) = static_cast<u64>(NOW) + TWO_SECONDS;
    const Built built = Build(hud, cooling);
    CHECK(built.lines.at(HEALTH_LINE).text == "生命 300/400");
    CHECK(built.lines.at(QI_LINE).text == "气 50/100");
    CHECK(built.lines.at(FIRST_SLOT_LINE).text == "左键  火弹  2秒");
    CHECK(built.lines.at(SPENDER_LINE).text == "右键  毒枪");
    CHECK(built.lines.back().text == "5  —");
    hud.Play({ .kind = core::EventKind::NotEnoughQi }, NOW);
    CHECK(Build(hud).lines.at(QI_LINE).text == "气不足");
}

TEST_CASE("the HUD grows with the text size, 16 pixels in from the edges") {
    constexpr f32 DOUBLE = 2.0F;
    app::Hud hud(HudStrings(), { .language = core::Language::English, .scale = DOUBLE }, core::UiColours{});
    const Built built = Build(hud);
    CHECK(built.bars.at(HEALTH_BAR).topLeft.x == app::HUD_MARGIN);
    CHECK(built.bars.at(HEALTH_BAR).size.x == app::HUD_BAR_WIDTH * DOUBLE);
    CHECK(built.bars.at(HEALTH_BAR).size.y == app::HUD_BAR_HEIGHT * DOUBLE);
    CHECK(built.lines.back().topLeft.y + (app::HUD_LINE_HEIGHT * DOUBLE) == static_cast<f32>(HEIGHT) - app::HUD_MARGIN);
}

TEST_CASE("the HUD follows the target's height: the same layout, moved down with its bottom") {
    constexpr u32 TALLER = 1'080;
    constexpr f32 DOWN = static_cast<f32>(TALLER - HEIGHT);
    app::Hud hud = English();
    const Built shorter = Build(hud);
    Built taller;
    hud.Build(CASTER, NOW, TALLER, taller.lines, taller.bars);
    REQUIRE(taller.lines.size() == shorter.lines.size());
    REQUIRE(taller.bars.size() == shorter.bars.size());
    for (std::size_t line = 0; line < taller.lines.size(); ++line) {
        CAPTURE(line);
        CHECK(taller.lines.at(line).topLeft.x == shorter.lines.at(line).topLeft.x);
        CHECK(taller.lines.at(line).topLeft.y == shorter.lines.at(line).topLeft.y + DOWN);
    }
    for (std::size_t bar = 0; bar < taller.bars.size(); ++bar) {
        CAPTURE(bar);
        CHECK(taller.bars.at(bar).topLeft.x == app::HUD_MARGIN);
        CHECK(taller.bars.at(bar).topLeft.y == shorter.bars.at(bar).topLeft.y + DOWN);
    }
    CHECK(taller.lines.back().topLeft.y + app::HUD_LINE_HEIGHT == static_cast<f32>(TALLER) - app::HUD_MARGIN);
}

// A HUD table: each colour the HUD reads, distinct, starting from `first`.
core::UiColours HudColours(u32 first) {
    core::UiColours colours;
    u32 next = first;
    for (const core::UiColour colour : { core::UiColour::HealthBar,
                                         core::UiColour::QiBar,
                                         core::UiColour::BarBackground,
                                         core::UiColour::HudText,
                                         core::UiColour::HudCooling,
                                         core::UiColour::HudWarning,
                                         core::UiColour::TextInk }) {
        colours.Set(colour, next++);
    }
    return colours;
}

// CHECKs that `built`, a HUD with its spender cooling and the Qi warning showing, wears `colours`.
void CheckWears(const Built& built, const core::UiColours& colours) {
    CHECK(built.bars.at(HEALTH_BAR).fill == colours.Of(core::UiColour::HealthBar));
    CHECK(built.bars.at(QI_BAR).fill == colours.Of(core::UiColour::QiBar));
    CHECK(built.bars.at(HEALTH_BAR).background == colours.Of(core::UiColour::BarBackground));
    CHECK(built.bars.at(QI_BAR).background == colours.Of(core::UiColour::BarBackground));
    CHECK(built.lines.at(HEALTH_LINE).colour == colours.Of(core::UiColour::HudText));
    CHECK(built.lines.at(QI_LINE).colour == colours.Of(core::UiColour::HudWarning));
    CHECK(built.lines.at(SPENDER_LINE).colour == colours.Of(core::UiColour::HudCooling));
    for (std::size_t line = FIRST_SLOT_LINE; line < built.lines.size(); ++line) {
        if (line != SPENDER_LINE) {
            CAPTURE(line);
            CHECK(built.lines.at(line).colour == colours.Of(core::UiColour::HudText));
        }
    }
}

TEST_CASE("the HUD's lines and bars take the table's colours, and a new table recolours them at once") {
    constexpr u32 FIRST = 0xFF00'0001U;
    constexpr u32 OTHERS = 0xFF00'0101U;
    const core::UiColours colours = HudColours(FIRST);
    const core::UiColours others = HudColours(OTHERS);
    app::Hud hud = English(colours);
    hud.Play({ .kind = core::EventKind::NotEnoughQi }, NOW);
    CheckWears(Build(hud, SpenderCooling()), colours);
    hud.SetColours(others);
    CheckWears(Build(hud, SpenderCooling()), others);
}

TEST_CASE("with the outline and the shadow off, the slot lines take the ink, a cooling one faded; the bars' words don't") {
    constexpr u32 FIRST = 0xFF00'0001U;
    constexpr u32 OTHERS = 0xFF00'0101U;
    const core::UiColours colours = HudColours(FIRST);
    app::Hud hud(HudStrings(), { .language = core::Language::English, .outline = false, .shadow = false }, colours);
    const auto check = [&hud](const core::UiColours& table) {
        const Built built = Build(hud, SpenderCooling());
        const u32 ink = table.Of(core::UiColour::TextInk);
        CHECK(built.lines.at(HEALTH_LINE).colour == table.Of(core::UiColour::HudText)); // on its bar
        CHECK(built.lines.at(QI_LINE).colour == table.Of(core::UiColour::HudText));
        CHECK(built.lines.at(SPENDER_LINE).colour == core::ScaledColour(ink, app::HUD_COOLING_INK_OPACITY));
        CHECK(built.lines.at(SPENDER_LINE).text == "Right click  Poison Lance  3s"); // still its seconds
        for (std::size_t line = FIRST_SLOT_LINE; line < built.lines.size(); ++line) {
            if (line != SPENDER_LINE) {
                CAPTURE(line);
                CHECK(built.lines.at(line).colour == ink);
            }
        }
    };
    check(colours);
    hud.SetColours(HudColours(OTHERS));
    check(HudColours(OTHERS));

    // Either one on, the light colours stay, as the labels' do.
    hud.SetTextOptions({ .language = core::Language::English, .outline = true, .shadow = false });
    const Built outlined = Build(hud, SpenderCooling());
    CHECK(outlined.lines.at(FIRST_SLOT_LINE).colour == HudColours(OTHERS).Of(core::UiColour::HudText));
    CHECK(outlined.lines.at(SPENDER_LINE).colour == HudColours(OTHERS).Of(core::UiColour::HudCooling));
}

} // namespace
