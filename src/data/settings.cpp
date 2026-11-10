#include "data/settings.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "data/internal/yaml.hpp"

namespace data {

namespace {

constexpr i32 MIN_WIDTH = 320;
constexpr i32 MIN_HEIGHT = 200;
constexpr i32 MAX_SIZE = 16'384;

// Reads `key` into `value` when the map has it; yaml-cpp throws on a value of the wrong type.
template<typename T>
void ReadIfPresent(const YAML::Node& map, const char* key, T& value) {
    if (const YAML::Node node = map[key]) {
        value = node.as<T>();
    }
}

// The line (from 1) of `key` in `root`: a null value's own mark lies after its key's line, and a scalar may sit below.
i32 KeyLine(const YAML::Node& root, std::string_view key) {
    for (const auto& entry : root) {
        if (entry.first.as<std::string>() == key) {
            return entry.first.Mark().line + 1;
        }
    }
    return root.Mark().line + 1;
}

// Whether `node` holds some text to read (not nothing, an empty string or a list or map).
bool HasText(const YAML::Node& node) {
    return node.IsScalar() && !node.Scalar().empty();
}

// The index in `names` of the text of `root`'s `key`; none, with a warning, for a name that isn't there (`what` names
// the kind of name, `fallback` the choice used instead).
template<std::size_t N>
std::optional<std::size_t> ReadName(
    const YAML::Node& root,
    const char* key,
    const std::array<std::string_view, N>& names,
    std::string_view what,
    std::string_view fallback,
    std::vector<std::string>* warnings
) {
    const YAML::Node node = root[key];
    if (HasText(node)) {
        const std::string& text = node.Scalar();
        if (const auto known = std::ranges::find(names, text); known != names.end()) {
            return static_cast<std::size_t>(known - names.begin());
        }
        if (warnings != nullptr) {
            warnings->push_back(
                std::format("settings line {}: there is no {} {}; {} is used", KeyLine(root, key), what, text, fallback)
            );
        }
    }
    else if (warnings != nullptr) {
        warnings->push_back(
            std::format("settings line {}: {} has no {} to read; {} is used", KeyLine(root, key), key, what, fallback)
        );
    }
    return std::nullopt;
}

// Reads a boolean setting; one that isn't a boolean keeps its default, with a warning.
void ReadBoolean(const YAML::Node& root, const char* key, bool& value, std::vector<std::string>* warnings) {
    const YAML::Node node = root[key];
    if (!node) {
        return;
    }
    bool read = false;
    if (YAML::convert<bool>::decode(node, read)) {
        value = read;
    }
    else if (warnings != nullptr) {
        if (HasText(node)) {
            warnings->push_back(
                std::format(
                    "settings line {}: {} is true or false, not {}; {} is used", KeyLine(root, key), key, node.Scalar(), value
                )
            );
        }
        else {
            warnings->push_back(
                std::format("settings line {}: {} isn't true or false; {} is used", KeyLine(root, key), key, value)
            );
        }
    }
}

} // namespace

std::expected<core::Settings, core::Error> ParseSettings(std::string_view yaml, std::vector<std::string>* warnings) {
    core::Settings settings;
    try {
        const YAML::Node root = YAML::Load(std::string{ yaml });
        if (root.IsNull()) {
            return settings;
        }
        if (!root.IsMap()) {
            return std::unexpected{ core::Error{
                std::format("settings line {}: not a map of settings", root.Mark().line + 1) } };
        }
        internal::NoteUnknownKeys(
            "settings",
            root,
            "",
            "setting",
            { "window", "language", "combined_damage_numbers", "damage_number_motion", "text_outline", "text_shadow" },
            warnings
        );
        if (root["language"]) {
            if (const std::optional<std::size_t> known =
                    ReadName(root, "language", core::LANGUAGE_CODES, "language", "English", warnings)) {
                settings.language = static_cast<core::Language>(*known);
            }
        }
        if (root["damage_number_motion"]) {
            if (const std::optional<std::size_t> known =
                    ReadName(root, "damage_number_motion", core::NUMBER_MOTION_NAMES, "number motion", "bounce", warnings)) {
                settings.damageNumberMotion = static_cast<core::NumberMotion>(*known);
            }
        }
        ReadBoolean(root, "combined_damage_numbers", settings.combinedDamageNumbers, warnings);
        ReadBoolean(root, "text_outline", settings.textOutline, warnings);
        ReadBoolean(root, "text_shadow", settings.textShadow, warnings);
        if (const YAML::Node window = root["window"]) {
            internal::NoteUnknownKeys("settings", window, "window", "setting", { "width", "height", "fullscreen" }, warnings);
            ReadIfPresent(window, "width", settings.windowWidth);
            ReadIfPresent(window, "height", settings.windowHeight);
            ReadIfPresent(window, "fullscreen", settings.fullscreen);
            if (settings.windowWidth < MIN_WIDTH || settings.windowHeight < MIN_HEIGHT || settings.windowWidth > MAX_SIZE ||
                settings.windowHeight > MAX_SIZE) {
                return std::unexpected{ core::Error{ std::format(
                    "settings line {}: a window of {}x{}, not within {}x{} to {}x{}",
                    window.Mark().line + 1,
                    settings.windowWidth,
                    settings.windowHeight,
                    MIN_WIDTH,
                    MIN_HEIGHT,
                    MAX_SIZE,
                    MAX_SIZE
                ) } };
            }
        }
    }
    catch (const YAML::Exception& error) {
        return std::unexpected{ internal::ErrorOf("settings", error) };
    }
    return settings;
}

std::expected<void, core::Error> SaveSettings(const std::filesystem::path& path, const core::Settings& settings) {
    return internal::WriteText(
        path,
        std::format(
            "# project_n's settings. Delete this file to get the defaults back.\n"
            "window:\n"
            "  width: {}  # pixels, when not fullscreen\n"
            "  height: {}\n"
            "  fullscreen: {}  # true or false\n"
            "language: {}  # en (English) or zh (Simplified Chinese)\n"
            "combined_damage_numbers: {}  # true: one summed number per creature and moment; false: one per hit\n"
            "damage_number_motion: {}  # bounce or rise\n"
            "text_outline: {}  # true or false\n"
            "text_shadow: {}  # true or false\n",
            settings.windowWidth,
            settings.windowHeight,
            settings.fullscreen,
            core::LANGUAGE_CODES.at(std::to_underlying(settings.language)),
            settings.combinedDamageNumbers,
            core::NUMBER_MOTION_NAMES.at(std::to_underlying(settings.damageNumberMotion)),
            settings.textOutline,
            settings.textShadow
        )
    );
}

core::Settings LoadSettings(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error) && !error) {
        const core::Settings defaults;
        const std::expected<void, core::Error> saved = SaveSettings(path, defaults);
        if (saved) {
            PN_LOG_INFO("no settings yet: wrote the defaults to {}", path.string());
        }
        else {
            PN_LOG_WARNING("no settings yet, and the defaults weren't written: {}", saved.error().message);
        }
        return defaults;
    }
    std::vector<std::string> warnings;
    const std::expected<core::Settings, core::Error> settings =
        internal::ReadText(path).and_then([&warnings](const std::string& text) { return ParseSettings(text, &warnings); });
    for (const std::string& warning : warnings) {
        PN_LOG_WARNING("{}", warning);
    }
    if (!settings) {
        PN_LOG_WARNING("using the default settings, {} left as it is: {}", path.string(), settings.error().message);
        return {};
    }
    PN_LOG_INFO(
        "settings from {}: window {}x{}{}, language {}",
        path.string(),
        settings->windowWidth,
        settings->windowHeight,
        settings->fullscreen ? ", fullscreen" : "",
        core::LANGUAGE_CODES.at(std::to_underlying(settings->language))
    );
    return *settings;
}

} // namespace data
