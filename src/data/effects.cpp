#include "data/effects.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/tick_clock.hpp"
#include "data/internal/yaml.hpp"

namespace data {

namespace {

constexpr f64 TICKS_A_SECOND = static_cast<f64>(core::TICKS_PER_SECOND);

core::Error RefuseEffects(const YAML::Node& node, std::string_view why) {
    return { std::format("effects line {}: {}", node.Mark().line + 1, why) };
}

// Reads a map of numbers into f32 fields, remembering the first refusal: each `read` leaves its field as it was when its key
// is absent.
class EffectFields {
public:
    EffectFields(const YAML::Node& map, std::string_view name)
      : m_map{ map }
      , m_name{ name } {
        m_error =
            map.IsMap() ? std::nullopt : std::optional<core::Error>{ RefuseEffects(map, std::format("{} isn't a map", name)) };
    }

    // Sets `field` from `key` within `range`, in `scale` units of the file to one of the field.
    void Read(const char* key, f32& field, const internal::Range& range, f64 scale = 1.0) {
        if (m_error) {
            return;
        }
        const std::expected<f64, core::Error> value =
            internal::TunedNumber("effects", m_map, m_name, key, static_cast<f64>(field) / scale, range);
        if (!value) {
            m_error = value.error();
            return;
        }
        if (m_map[key]) {
            field = static_cast<f32>(*value * scale);
        }
    }

    void Read(const char* key, f64& field, const internal::Range& range) {
        if (m_error) {
            return;
        }
        const std::expected<f64, core::Error> value = internal::TunedNumber("effects", m_map, m_name, key, field, range);
        if (!value) {
            m_error = value.error();
            return;
        }
        field = *value;
    }

    [[nodiscard]] const std::optional<core::Error>& Error() const {
        return m_error;
    }

private:
    YAML::Node m_map;
    std::string_view m_name;
    std::optional<core::Error> m_error = std::nullopt;
};

constexpr internal::Range POSITIVE{ .low = 0.0, .lowOpen = true };
constexpr internal::Range NOT_NEGATIVE{ .low = 0.0 };
constexpr internal::Range FRACTION{ .low = 0.0, .lowOpen = false, .high = 1.0 };

std::expected<void, core::Error> ReadSwords(
    const YAML::Node& node,
    const char* name,
    std::vector<std::string>* warnings,
    core::SwordLook& look
) {
    internal::NoteUnknownKeys(
        "effects",
        node,
        name,
        "key",
        { "sword_scale", "halo_scale", "core_scale", "trail", "trail_width", "trail_taper", "halo_intensity", "core_opacity" },
        warnings
    );
    EffectFields fields(node, name);
    fields.Read("sword_scale", look.swordScale, POSITIVE);
    fields.Read("halo_scale", look.haloScale, POSITIVE);
    fields.Read("core_scale", look.coreScale, POSITIVE);
    fields.Read("trail", look.trailTicks, POSITIVE, TICKS_A_SECOND);
    fields.Read("trail_width", look.trailWidth, POSITIVE);
    fields.Read("trail_taper", look.trailTaper, FRACTION);
    fields.Read("halo_intensity", look.haloIntensity, NOT_NEGATIVE);
    fields.Read("core_opacity", look.coreOpacity, FRACTION);
    if (fields.Error()) {
        return std::unexpected{ *fields.Error() };
    }
    return {};
}

std::expected<void, core::Error> ReadBurst(const YAML::Node& node, std::vector<std::string>* warnings, core::BurstLook& look) {
    internal::NoteUnknownKeys(
        "effects", node, "poison_lance", "key", { "flash_start_scale", "flash_end_scale", "flash_intensity" }, warnings
    );
    EffectFields fields(node, "poison_lance");
    fields.Read("flash_start_scale", look.startScale, POSITIVE);
    fields.Read("flash_end_scale", look.endScale, POSITIVE);
    fields.Read("flash_intensity", look.intensity, NOT_NEGATIVE);
    if (fields.Error()) {
        return std::unexpected{ *fields.Error() };
    }
    return {};
}

std::expected<void, core::Error> ReadAbilities(
    const YAML::Node& node,
    std::vector<std::string>* warnings,
    core::Effects& effects
) {
    if (!node.IsMap()) {
        return std::unexpected{ RefuseEffects(node, "abilities isn't a map") };
    }
    internal::NoteUnknownKeys(
        "effects", node, "abilities", "ability", { "fire_bolt", "flying_swords", "poison_lance" }, warnings
    );
    if (const YAML::Node bolt = node["fire_bolt"]) {
        if (const std::expected<void, core::Error> read = ReadSwords(bolt, "fire_bolt", warnings, effects.fireBolt); !read) {
            return read;
        }
    }
    if (const YAML::Node swords = node["flying_swords"]) {
        if (const std::expected<void, core::Error> read = ReadSwords(swords, "flying_swords", warnings, effects.flyingSwords);
            !read) {
            return read;
        }
    }
    if (const YAML::Node lance = node["poison_lance"]) {
        return ReadBurst(lance, warnings, effects.poisonLance);
    }
    return {};
}

std::expected<void, core::Error> ReadHealthBar(
    const YAML::Node& node,
    std::vector<std::string>* warnings,
    core::HealthBarLook& look
) {
    internal::NoteUnknownKeys(
        "effects", node, "health_bar", "key", { "rise", "width", "height", "fill_width", "fill_height" }, warnings
    );
    EffectFields fields(node, "health_bar");
    fields.Read("rise", look.rise, NOT_NEGATIVE);
    fields.Read("width", look.width, POSITIVE);
    fields.Read("height", look.height, POSITIVE);
    fields.Read("fill_width", look.fillWidth, POSITIVE);
    fields.Read("fill_height", look.fillHeight, POSITIVE);
    if (fields.Error()) {
        return std::unexpected{ *fields.Error() };
    }
    return {};
}

std::expected<void, core::Error> ReadHud(const YAML::Node& node, std::vector<std::string>* warnings, core::HudLayout& layout) {
    internal::NoteUnknownKeys(
        "effects",
        node,
        "hud",
        "key",
        { "margin",
          "bar_width",
          "bar_height",
          "line_height",
          "bar_gap",
          "bar_text_inset",
          "bar_text_rise",
          "not_enough_qi",
          "cooling_ink_opacity" },
        warnings
    );
    EffectFields fields(node, "hud");
    fields.Read("margin", layout.margin, NOT_NEGATIVE);
    fields.Read("bar_width", layout.barWidth, POSITIVE);
    fields.Read("bar_height", layout.barHeight, POSITIVE);
    fields.Read("line_height", layout.lineHeight, POSITIVE);
    fields.Read("bar_gap", layout.barGap, NOT_NEGATIVE);
    fields.Read("bar_text_inset", layout.barTextInset, NOT_NEGATIVE);
    fields.Read("bar_text_rise", layout.barTextRise, NOT_NEGATIVE);
    fields.Read("not_enough_qi", layout.notEnoughQiSeconds, POSITIVE);
    fields.Read("cooling_ink_opacity", layout.coolingInkOpacity, FRACTION);
    if (fields.Error()) {
        return std::unexpected{ *fields.Error() };
    }
    return {};
}

} // namespace

std::expected<core::Effects, core::Error> ParseEffects(std::string_view effects, std::vector<std::string>* warnings) {
    try {
        const YAML::Node root = YAML::Load(std::string{ effects });
        if (!root.IsMap()) {
            return std::unexpected{ RefuseEffects(root, "not a map of effects") };
        }
        internal::NoteUnknownKeys("effects", root, "", "section", { "abilities", "health_bar", "hud" }, warnings);
        core::Effects table;
        if (const YAML::Node abilities = root["abilities"]) {
            if (const std::expected<void, core::Error> read = ReadAbilities(abilities, warnings, table); !read) {
                return std::unexpected{ read.error() };
            }
        }
        if (const YAML::Node bar = root["health_bar"]) {
            if (const std::expected<void, core::Error> read = ReadHealthBar(bar, warnings, table.healthBar); !read) {
                return std::unexpected{ read.error() };
            }
        }
        if (const YAML::Node hud = root["hud"]) {
            if (const std::expected<void, core::Error> read = ReadHud(hud, warnings, table.hud); !read) {
                return std::unexpected{ read.error() };
            }
        }
        return table;
    }
    catch (const YAML::Exception& error) {
        return std::unexpected{ internal::ErrorOf("effects", error) };
    }
}

std::expected<core::Effects, core::Error> LoadEffects(const std::filesystem::path& folder) {
    std::vector<std::string> warnings;
    std::expected<core::Effects, core::Error> effects =
        internal::ReadText(folder / "effects.yaml").and_then([&](const std::string& text) {
            return ParseEffects(text, &warnings);
        });
    for (const std::string& warning : warnings) {
        PN_LOG_WARNING("{}", warning);
    }
    return effects;
}

} // namespace data
