#include "app/hud.hpp"

#include "core/abilities.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"
#include "core/tick_clock.hpp"

namespace app {

namespace {

// Between a slot line's control, its ability and its seconds left.
constexpr std::string_view SEPARATOR = "  ";
// The specials' and the ultimate's keys, from Slot::Special1 on: their digits read the same in every language.
constexpr std::array<std::string_view, 5> DIGIT_KEYS{ { "1", "2", "3", "4", "5" } };

// The string naming the control that casts `slot`, if a string names it: the specials' and the ultimate's are digits.
std::optional<core::StringId> KeyOf(core::Slot slot) {
    if (slot == core::Slot::Generator) {
        return core::StringId::KeyLeftClick;
    }
    if (slot == core::Slot::Spender) {
        return core::StringId::KeyRightClick;
    }
    if (slot == core::Slot::Movement) {
        return core::StringId::KeySpace;
    }
    return std::nullopt;
}

// The string naming `ability`, or the empty slot's for None.
core::StringId NameOf(core::AbilityKind ability) {
    if (ability == core::AbilityKind::FireBolt) {
        return core::StringId::FireBolt;
    }
    if (ability == core::AbilityKind::PoisonLance) {
        return core::StringId::PoisonLance;
    }
    if (ability == core::AbilityKind::Dash) {
        return core::StringId::Dash;
    }
    if (ability == core::AbilityKind::FlyingSwords) {
        return core::StringId::FlyingSwords;
    }
    return core::StringId::SlotEmpty;
}

// `value` over `most`, within 0 to 1; 0 with no most.
f32 FractionOf(i64 value, i64 most) {
    if (most <= 0) {
        return 0.0F;
    }
    return static_cast<f32>(std::clamp(static_cast<f64>(value) / static_cast<f64>(most), 0.0, 1.0));
}

} // namespace

Hud::Hud(std::shared_ptr<const core::Strings> strings, const gfx::TextOptions& text, const core::UiColours& colours)
  : m_strings{ std::move(strings) }
  , m_text{ text }
  , m_colours{ colours } {
}

void Hud::SetColours(const core::UiColours& colours) {
    m_colours = colours;
}

void Hud::SetEffects(const core::Effects& effects) {
    m_layout = effects.hud;
}

void Hud::SetTextOptions(const gfx::TextOptions& text) {
    m_text = text;
}

void Hud::Play(const core::Event& event, f64 renderTick) {
    if (event.kind == core::EventKind::NotEnoughQi) {
        m_warnedUntil = renderTick + (m_layout.notEnoughQiSeconds * static_cast<f64>(core::TICKS_PER_SECOND));
    }
}

void Hud::Clear() {
    m_warnedUntil.reset();
}

const std::string& Hud::Text(core::StringId string) const {
    return m_strings->Text(string, m_text.language);
}

void Hud::Build(
    const core::CasterView& caster,
    f64 renderTick,
    u32 height,
    std::vector<gfx::HudLine>& lines,
    std::vector<gfx::HudBar>& bars
) {
    PN_PROFILE_ZONE();
    const u32 text = m_colours.Of(core::UiColour::HudText);
    // Over the ground, light text needs its dark edge to read on the pale ground; without one the slots turn to the ink,
    // as opaque as the text was, as the labels do. The bars' words stay light, on their bars.
    const bool inked = !m_text.outline && !m_text.shadow;
    const u32 ink = core::ScaledColour(m_colours.Of(core::UiColour::TextInk), core::OpacityOf(text));
    const u32 slotText = inked ? ink : text;
    const core::HudLayout& layout = m_layout;
    const u32 coolingText =
        inked ? core::ScaledColour(ink, layout.coolingInkOpacity) : m_colours.Of(core::UiColour::HudCooling);
    const f32 lineHeight = layout.lineHeight * m_text.scale;
    const core::Vec2 barSize{ .x = layout.barWidth * m_text.scale, .y = layout.barHeight * m_text.scale };
    // From the bottom up: the slots' lines, then the Qi bar over them, then the health bar over it.
    const f32 firstSlot = static_cast<f32>(height) - layout.margin - (lineHeight * static_cast<f32>(core::SLOTS));
    const f32 qiTop = firstSlot - ((layout.barGap * m_text.scale) + barSize.y);
    const f32 healthTop = qiTop - ((layout.barGap * m_text.scale) + barSize.y);

    const bool warned = m_warnedUntil && renderTick < *m_warnedUntil;
    std::string& healthText = m_texts.at(0);
    std::string& qiText = m_texts.at(1);
    core::FillValue(healthText, Text(core::StringId::Health), caster.health, caster.healthMax);
    if (warned) {
        qiText = Text(core::StringId::NotEnoughQi);
    }
    else {
        core::FillValue(qiText, Text(core::StringId::Qi), caster.qi, caster.qiMax);
    }
    const auto barText = [&](const std::string& says, f32 top, u32 colour) {
        lines.push_back(
            { .text = says,
              .topLeft = { .x = layout.margin + (layout.barTextInset * m_text.scale),
                           .y = top - (layout.barTextRise * m_text.scale) },
              .colour = colour }
        );
    };
    barText(healthText, healthTop, text);
    barText(qiText, qiTop, warned ? m_colours.Of(core::UiColour::HudWarning) : text);

    for (std::size_t index = 0; index < core::SLOTS; ++index) {
        const auto slot = static_cast<core::Slot>(index);
        std::string& line = m_texts.at(2 + index);
        const std::optional<core::StringId> key = KeyOf(slot);
        line = key ? std::string_view{ Text(*key) } : DIGIT_KEYS.at(index - std::to_underlying(core::Slot::Special1));
        line += SEPARATOR;
        line += Text(NameOf(caster.loadout.at(index)));
        const f64 left = static_cast<f64>(caster.readyTick.at(index)) - renderTick;
        const bool cooling = left > 0.0;
        if (cooling) {
            const auto seconds = static_cast<i64>(std::ceil(left / static_cast<f64>(core::TICKS_PER_SECOND)));
            core::FillValue(m_seconds, Text(core::StringId::Cooldown), seconds, 0);
            line += SEPARATOR;
            line += m_seconds;
        }
        lines.push_back(
            { .text = line,
              .topLeft = { .x = layout.margin, .y = firstSlot + (lineHeight * static_cast<f32>(index)) },
              .colour = cooling ? coolingText : slotText }
        );
    }

    const u32 background = m_colours.Of(core::UiColour::BarBackground);
    bars.push_back(
        { .topLeft = { .x = layout.margin, .y = healthTop },
          .size = barSize,
          .fraction = FractionOf(caster.health, caster.healthMax),
          .fill = m_colours.Of(core::UiColour::HealthBar),
          .background = background }
    );
    bars.push_back(
        { .topLeft = { .x = layout.margin, .y = qiTop },
          .size = barSize,
          .fraction = FractionOf(caster.qi, caster.qiMax),
          .fill = m_colours.Of(core::UiColour::QiBar),
          .background = background }
    );
}

} // namespace app
