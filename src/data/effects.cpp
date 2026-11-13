#include "data/effects.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/glow.hpp"
#include "core/log.hpp"
#include "core/tick_clock.hpp"
#include "data/internal/yaml.hpp"

namespace data {

namespace {

constexpr f64 TICKS_A_SECOND = static_cast<f64>(core::TICKS_PER_SECOND);

core::Error RefuseEffects(const YAML::Node& node, std::string_view why) {
    return internal::RefusalAt("effects", node, why);
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

std::expected<void, core::Error> ReadSwords(
    const YAML::Node& node,
    const char* name,
    std::vector<std::string>* warnings,
    core::SwordLook& look
) {
    internal::NoteUnknownKeys(
        "effects", node, name, "key", { "sword_scale", "trail", "trail_width", "trail_taper", "trail_intensity" }, warnings
    );
    EffectFields fields(node, name);
    fields.Read("sword_scale", look.swordScale, POSITIVE);
    fields.Read("trail", look.trailTicks, POSITIVE, TICKS_A_SECOND);
    fields.Read("trail_width", look.trailWidth, POSITIVE);
    fields.Read("trail_taper", look.trailTaper, internal::FRACTION);
    fields.Read("trail_intensity", look.trailIntensity, NOT_NEGATIVE);
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
    fields.Read("cooling_ink_opacity", layout.coolingInkOpacity, internal::FRACTION);
    if (fields.Error()) {
        return std::unexpected{ *fields.Error() };
    }
    return {};
}

constexpr internal::Range REACH{ .low = 0.0, .lowOpen = true, .high = static_cast<f64>(core::MAX_GLOW_SIZE) };
constexpr internal::Range BEAT{ .low = static_cast<f64>(core::MIN_PULSE_SECONDS) };

constexpr std::array<std::string_view, 2> BLEND_NAMES{ { "light", "ink" } };
constexpr std::array<std::string_view, 4> KIND_NAMES{ { "steady", "pulse", "charge", "progress" } };

std::expected<void, core::Error> ReadDrive(
    const YAML::Node& node,
    const std::string& glow,
    std::vector<std::string>* warnings,
    core::GlowDrive& drive
) {
    const std::string owner = std::format("{}'s drive", glow);
    internal::NoteUnknownKeys(
        "effects", node, owner, "key", { "kind", "period", "depth", "curve", "fade", "fade_curve", "results" }, warnings
    );
    EffectFields fields(node, owner);
    fields.Read("period", drive.periodSeconds, BEAT);
    fields.Read("depth", drive.depth, internal::FRACTION);
    fields.Read("fade", drive.fadeSeconds, POSITIVE);
    if (fields.Error()) {
        return std::unexpected{ *fields.Error() };
    }
    if (const std::expected<void, core::Error> read =
            internal::ReadNamed("effects", node, owner, "kind", KIND_NAMES, drive.kind);
        !read) {
        return read;
    }
    if (const std::expected<void, core::Error> read =
            internal::ReadNamed("effects", node, owner, "curve", internal::CURVE_NAMES, drive.curve);
        !read) {
        return read;
    }
    if (const std::expected<void, core::Error> read =
            internal::ReadNamed("effects", node, owner, "fade_curve", internal::CURVE_NAMES, drive.fadeCurve);
        !read) {
        return read;
    }
    if (const YAML::Node results = node["results"]) {
        if (!results.IsSequence() || results.size() > core::MAX_GLOW_RESULTS) {
            return std::unexpected{ RefuseEffects(
                results, std::format("{}'s results isn't a list of at most {} colours", owner, core::MAX_GLOW_RESULTS)
            ) };
        }
        drive.resultCount = 0;
        for (const YAML::Node& entry : results) {
            const std::expected<core::UiColour, core::Error> colour = internal::ReadPalette("effects", entry, owner);
            if (!colour) {
                return std::unexpected{ colour.error() };
            }
            drive.results.at(drive.resultCount) = *colour;
            ++drive.resultCount;
        }
    }
    return {};
}

std::expected<void, core::Error> ReadGlow(
    const YAML::Node& node,
    const std::string& name,
    std::vector<std::string>* warnings,
    core::GlowLook& look
) {
    internal::NoteUnknownKeys(
        "effects", node, name, "key", { "colour", "blend", "size", "feather", "opacity", "zoom_shrink", "drive" }, warnings
    );
    EffectFields fields(node, name);
    fields.Read("size", look.size, REACH);
    fields.Read("feather", look.feather, internal::FRACTION);
    fields.Read("opacity", look.opacity, internal::FRACTION);
    fields.Read("zoom_shrink", look.zoomShrink, internal::FRACTION);
    if (fields.Error()) {
        return std::unexpected{ *fields.Error() };
    }
    if (const YAML::Node colour = node["colour"]) {
        const std::expected<core::GlowColour, core::Error> read = internal::ReadGlowColour("effects", colour, name);
        if (!read) {
            return std::unexpected{ read.error() };
        }
        // `element` keeps the palette colour the look had, as it always has: only the choice changes.
        if (read->element) {
            look.colour.element = true;
        }
        else {
            look.colour = *read;
        }
    }
    if (const std::expected<void, core::Error> read =
            internal::ReadNamed("effects", node, name, "blend", BLEND_NAMES, look.blend);
        !read) {
        return read;
    }
    if (const YAML::Node drive = node["drive"]) {
        if (!drive.IsMap()) {
            return std::unexpected{ RefuseEffects(drive, std::format("{}'s drive isn't a map", name)) };
        }
        return ReadDrive(drive, name, warnings, look.drive);
    }
    return {};
}

bool IsRoleKey(std::string_view key) {
    return !key.empty() && std::ranges::find(core::GLOW_ROLE_KEYS, key) != core::GLOW_ROLE_KEYS.end();
}

using NamedGlows = std::vector<std::pair<std::string, core::GlowLook>>;

NamedGlows::iterator FindGlow(NamedGlows& named, const std::string& name) {
    return std::ranges::find_if(named, [&name](const auto& entry) { return entry.first == name; });
}

// Adds each glow of `glows` to `named`, replacing the one of its name.
std::expected<void, core::Error> ReadGlowTable(const YAML::Node& glows, std::vector<std::string>* warnings, NamedGlows& named) {
    if (!glows.IsMap()) {
        return std::unexpected{ RefuseEffects(glows, "glows isn't a map") };
    }
    for (const auto& entry : glows) {
        const auto name = entry.first.as<std::string>();
        if (!entry.second.IsMap()) {
            return std::unexpected{ RefuseEffects(entry.second, std::format("glow {} isn't a map", name)) };
        }
        const auto existing = FindGlow(named, name);
        core::GlowLook look = existing == named.end() ? core::GlowLook{} : existing->second;
        if (const std::expected<void, core::Error> read = ReadGlow(entry.second, name, warnings, look); !read) {
            return read;
        }
        if (existing == named.end()) {
            named.emplace_back(name, look);
        }
        else {
            existing->second = look;
        }
    }
    return {};
}

// Fills `roleGlows` (by role) from `roles`: every role must name a glow of `named`.
std::expected<void, core::Error> ReadRoles(
    const YAML::Node& roles,
    std::vector<std::string>* warnings,
    NamedGlows& named,
    std::array<std::string, core::GLOW_ROLES>& roleGlows
) {
    if (!roles.IsMap()) {
        return std::unexpected{ RefuseEffects(roles, "glow_roles isn't a map") };
    }
    for (const auto& entry : roles) {
        const auto key = entry.first.as<std::string>();
        if (warnings != nullptr && !IsRoleKey(key)) {
            warnings->push_back(
                std::format("effects line {}: glow_roles has no role {}; it is ignored", entry.first.Mark().line + 1, key)
            );
        }
    }
    for (std::size_t role = 1; role < core::GLOW_ROLES; ++role) {
        const std::string_view key = core::GLOW_ROLE_KEYS.at(role);
        const YAML::Node glow = roles[std::string{ key }];
        if (!glow) {
            return std::unexpected{ RefuseEffects(roles, std::format("glow_roles has no role {}", key)) };
        }
        roleGlows.at(role) = glow.IsScalar() ? glow.Scalar() : std::string{};
        if (FindGlow(named, roleGlows.at(role)) == named.end()) {
            return std::unexpected{ RefuseEffects(
                glow, std::format("glow_roles's {} names the glow \"{}\", which isn't there", key, roleGlows.at(role))
            ) };
        }
    }
    return {};
}

// The glows by name (the shipped ones, which `glows:` adds to or replaces) and which each role wears.
std::expected<void, core::Error> ReadGlows(const YAML::Node& root, std::vector<std::string>* warnings, core::Effects& table) {
    NamedGlows named;
    named.reserve(core::SHIPPED_GLOW_COUNT);
    for (const core::NamedGlow& glow : core::SHIPPED_GLOWS) {
        named.emplace_back(std::string{ glow.name }, glow.look);
    }
    if (const YAML::Node glows = root["glows"]) {
        if (const std::expected<void, core::Error> read = ReadGlowTable(glows, warnings, named); !read) {
            return read;
        }
    }
    std::array<std::string, core::GLOW_ROLES> roleGlows;
    for (std::size_t role = 1; role < core::GLOW_ROLES; ++role) {
        roleGlows.at(role) = core::SHIPPED_GLOW_ROLES.at(role);
    }
    if (const YAML::Node roles = root["glow_roles"]) {
        if (const std::expected<void, core::Error> read = ReadRoles(roles, warnings, named, roleGlows); !read) {
            return read;
        }
    }
    for (std::size_t role = 1; role < core::GLOW_ROLES; ++role) {
        table.glows.at(role) = FindGlow(named, roleGlows.at(role))->second;
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
        internal::NoteUnknownKeys(
            "effects", root, "", "section", { "abilities", "health_bar", "hud", "glows", "glow_roles" }, warnings
        );
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
        if (const std::expected<void, core::Error> read = ReadGlows(root, warnings, table); !read) {
            return std::unexpected{ read.error() };
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
