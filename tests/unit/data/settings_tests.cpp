#include "core/cxx23.hpp" // IWYU pragma: keep
#include "data/settings.hpp"

#include <doctest/doctest.h>

namespace {

// A folder of its own under the system's temporary folder, removed when the test ends.
class Scratch {
public:
    explicit Scratch(std::string_view name)
      : m_folder{ std::filesystem::temp_directory_path() / std::format("project_n_settings_tests_{}", name) } {
        std::filesystem::remove_all(m_folder);
        std::filesystem::create_directories(m_folder);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
    ~Scratch() {
        std::error_code ignored;
        std::filesystem::remove_all(m_folder, ignored);
    }

    [[nodiscard]] std::filesystem::path File() const {
        return m_folder / "settings.yaml";
    }

private:
    std::filesystem::path m_folder;
};

std::string Contents(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    return { std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

void Write(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output{ path, std::ios::binary };
    output << text;
}

TEST_CASE("settings parse, and a setting left out keeps its default") {
    constexpr std::string_view YAML = "window:\n  width: 1920\n  fullscreen: true\n";
    const std::expected<core::Settings, core::Error> settings = data::ParseSettings(YAML);
    REQUIRE(settings.has_value());
    CHECK(settings->windowWidth == 1920);
    CHECK(settings->windowHeight == core::Settings{}.windowHeight);
    CHECK(settings->fullscreen);
}

TEST_CASE("empty settings are the defaults") {
    const std::expected<core::Settings, core::Error> settings = data::ParseSettings("");
    REQUIRE(settings.has_value());
    CHECK(settings->windowWidth == core::Settings{}.windowWidth);
}

TEST_CASE("malformed settings are an error naming the line") {
    constexpr std::string_view BAD_YAML = "window:\n  width: [1920\n";
    constexpr std::string_view WRONG_TYPE = "window:\n  width: 1920\n  fullscreen: maybe\n";
    constexpr std::string_view TOO_SMALL = "window:\n  width: 10\n";
    constexpr std::string_view NOT_A_MAP = "- window\n";
    const std::expected<core::Settings, core::Error> badYaml = data::ParseSettings(BAD_YAML);
    const std::expected<core::Settings, core::Error> wrongType = data::ParseSettings(WRONG_TYPE);
    const std::expected<core::Settings, core::Error> tooSmall = data::ParseSettings(TOO_SMALL);
    const std::expected<core::Settings, core::Error> notAMap = data::ParseSettings(NOT_A_MAP);
    REQUIRE_FALSE(badYaml.has_value());
    REQUIRE_FALSE(wrongType.has_value());
    REQUIRE_FALSE(tooSmall.has_value());
    REQUIRE_FALSE(notAMap.has_value());
    CHECK(wrongType.error().message.contains("line 3"));
    CHECK(tooSmall.error().message.contains("10x720"));
    CHECK(notAMap.error().message.contains("line 1"));
}

TEST_CASE("saved settings read back the same") {
    const Scratch scratch{ "round_trip" };
    constexpr core::Settings SETTINGS{ .windowWidth = 2560, .windowHeight = 1440, .fullscreen = true };
    REQUIRE(data::SaveSettings(scratch.File(), SETTINGS).has_value());
    const std::expected<core::Settings, core::Error> read = data::ParseSettings(Contents(scratch.File()));
    REQUIRE(read.has_value());
    CHECK(read->windowWidth == SETTINGS.windowWidth);
    CHECK(read->windowHeight == SETTINGS.windowHeight);
    CHECK(read->fullscreen == SETTINGS.fullscreen);
}

TEST_CASE("missing settings give the defaults and write them out") {
    const Scratch scratch{ "missing" };
    const core::Settings settings = data::LoadSettings(scratch.File());
    CHECK(settings.windowWidth == core::Settings{}.windowWidth);
    REQUIRE(std::filesystem::exists(scratch.File()));
    CHECK(data::ParseSettings(Contents(scratch.File())).has_value());
}

TEST_CASE("a malformed settings file gives the defaults and is left alone") {
    const Scratch scratch{ "malformed" };
    constexpr std::string_view BROKEN = "window: {width: 1920\n";
    Write(scratch.File(), BROKEN);
    const core::Settings settings = data::LoadSettings(scratch.File());
    CHECK(settings.windowWidth == core::Settings{}.windowWidth);
    CHECK(Contents(scratch.File()) == BROKEN);
}

} // namespace
