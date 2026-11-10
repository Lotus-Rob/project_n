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
        internal::NoteUnknownKeys("settings", root, "", "setting", { "window", "language" }, warnings);
        if (const YAML::Node language = root["language"]) {
            const auto code = language.as<std::string>();
            const std::span<const std::string_view> codes{ core::LANGUAGE_CODES };
            if (const auto known = std::ranges::find(codes, code); known != codes.end()) {
                settings.language = static_cast<core::Language>(known - codes.begin());
            }
            else if (warnings != nullptr) {
                warnings->push_back(
                    std::format("settings line {}: there is no language {}; English is used", language.Mark().line + 1, code)
                );
            }
        }
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
            "language: {}  # en (English) or zh (Simplified Chinese)\n",
            settings.windowWidth,
            settings.windowHeight,
            settings.fullscreen,
            core::LANGUAGE_CODES.at(std::to_underlying(settings.language))
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
