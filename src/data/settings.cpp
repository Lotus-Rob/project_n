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

std::expected<core::Settings, core::Error> ParseSettings(std::string_view yaml) {
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
        if (const YAML::Node window = root["window"]) {
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
            "  fullscreen: {}  # true or false\n",
            settings.windowWidth,
            settings.windowHeight,
            settings.fullscreen
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
    const std::expected<core::Settings, core::Error> settings =
        internal::ReadText(path).and_then([](const std::string& text) { return ParseSettings(text); });
    if (!settings) {
        PN_LOG_WARNING("using the default settings, {} left as it is: {}", path.string(), settings.error().message);
        return {};
    }
    PN_LOG_INFO(
        "settings from {}: window {}x{}{}",
        path.string(),
        settings->windowWidth,
        settings->windowHeight,
        settings->fullscreen ? ", fullscreen" : ""
    );
    return *settings;
}

} // namespace data
