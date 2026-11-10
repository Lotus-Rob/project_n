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

TEST_CASE("an unknown setting is reported with its line, and the rest still parse") {
    constexpr std::string_view YAML = "window:\n  widht: 800\n  height: 600\nsound: loud\n";
    std::vector<std::string> warnings;
    const std::expected<core::Settings, core::Error> settings = data::ParseSettings(YAML, &warnings);
    REQUIRE(settings.has_value());
    CHECK(settings->windowHeight == 600);
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0] == "settings line 4: there is no setting sound; it is ignored");
    CHECK(warnings[1] == "settings line 2: window has no setting widht; it is ignored");
}

TEST_CASE("the settings SaveSettings writes hold no unknown key") {
    const Scratch scratch{ "known" };
    REQUIRE(data::SaveSettings(scratch.File(), {}).has_value());
    std::vector<std::string> warnings;
    const std::expected<core::Settings, core::Error> settings = data::ParseSettings(Contents(scratch.File()), &warnings);
    REQUIRE(settings.has_value());
    CHECK(warnings.empty());
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

TEST_CASE("the language setting picks a language, and an unknown one gives English with a warning") {
    const std::expected<core::Settings, core::Error> chinese = data::ParseSettings("language: zh\n");
    REQUIRE(chinese.has_value());
    CHECK(chinese->language == core::Language::Chinese);
    std::vector<std::string> warnings;
    const std::expected<core::Settings, core::Error> unknown = data::ParseSettings("language: klingon\n", &warnings);
    REQUIRE(unknown.has_value());
    CHECK(unknown->language == core::Language::English);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "settings line 1: there is no language klingon; English is used");
}

TEST_CASE("the text settings default to every hit, bouncing, outlined and shadowed") {
    const std::expected<core::Settings, core::Error> settings = data::ParseSettings("{}\n");
    REQUIRE(settings.has_value());
    CHECK_FALSE(settings->combinedDamageNumbers);
    CHECK(settings->damageNumberMotion == core::NumberMotion::Bounce);
    CHECK(settings->textOutline);
    CHECK(settings->textShadow);
}

TEST_CASE("the text settings are read") {
    const std::expected<core::Settings, core::Error> settings = data::ParseSettings(
        "combined_damage_numbers: true\ndamage_number_motion: rise\ntext_outline: false\ntext_shadow: false\n"
    );
    REQUIRE(settings.has_value());
    CHECK(settings->combinedDamageNumbers);
    CHECK(settings->damageNumberMotion == core::NumberMotion::Rise);
    CHECK_FALSE(settings->textOutline);
    CHECK_FALSE(settings->textShadow);
}

TEST_CASE("an unknown number motion gives bounce with a warning") {
    std::vector<std::string> warnings;
    const std::expected<core::Settings, core::Error> settings =
        data::ParseSettings("damage_number_motion: wobble\n", &warnings);
    REQUIRE(settings.has_value());
    CHECK(settings->damageNumberMotion == core::NumberMotion::Bounce);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "settings line 1: there is no number motion wobble; bounce is used");
}

TEST_CASE("a text setting that isn't a boolean keeps its default with a warning") {
    std::vector<std::string> warnings;
    const std::expected<core::Settings, core::Error> settings = data::ParseSettings("text_outline: maybe\n", &warnings);
    REQUIRE(settings.has_value());
    CHECK(settings->textOutline);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "settings line 1: text_outline is true or false, not maybe; true is used");
}

TEST_CASE("a text setting with no value keeps its default with a warning") {
    std::vector<std::string> warnings;
    const std::expected<core::Settings, core::Error> settings = data::ParseSettings("text_outline:\n", &warnings);
    REQUIRE(settings.has_value());
    CHECK(settings->textOutline);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "settings line 1: text_outline isn't true or false; true is used");
}

// The one warning that parsing `yaml` gives; the parse itself must succeed.
std::string SingleWarning(const std::string& yaml) {
    std::vector<std::string> warnings;
    const std::expected<core::Settings, core::Error> settings = data::ParseSettings(yaml, &warnings);
    REQUIRE(settings.has_value());
    REQUIRE(warnings.size() == 1);
    return warnings[0];
}

TEST_CASE("an empty language or number motion keeps its default with a warning") {
    const std::string language = SingleWarning("language:\n");
    const std::string motion = SingleWarning("damage_number_motion: \"\"\n");
    CHECK(language == "settings line 1: language has no language to read; English is used");
    CHECK(motion == "settings line 1: damage_number_motion has no number motion to read; bounce is used");
}

TEST_CASE("a text setting that is empty text or a list keeps its default with a warning") {
    const std::string empty = SingleWarning("text_outline: \"\"\n");
    const std::string list = SingleWarning("text_outline: [a]\n");
    CHECK(empty == "settings line 1: text_outline isn't true or false; true is used");
    CHECK(list == "settings line 1: text_outline isn't true or false; true is used");
}

TEST_CASE("a warning names the key's line when the value is below it") {
    const std::string warning = SingleWarning("text_outline:\n  maybe\n");
    CHECK(warning == "settings line 1: text_outline is true or false, not maybe; true is used");
}

TEST_CASE("saved settings read back the same") {
    const Scratch scratch{ "round_trip" };
    constexpr core::Settings SETTINGS{ .windowWidth = 2560,
                                       .windowHeight = 1440,
                                       .fullscreen = true,
                                       .language = core::Language::Chinese,
                                       .combinedDamageNumbers = true,
                                       .damageNumberMotion = core::NumberMotion::Rise,
                                       .textOutline = false,
                                       .textShadow = false };
    REQUIRE(data::SaveSettings(scratch.File(), SETTINGS).has_value());
    const std::expected<core::Settings, core::Error> read = data::ParseSettings(Contents(scratch.File()));
    REQUIRE(read.has_value());
    CHECK(read->windowWidth == SETTINGS.windowWidth);
    CHECK(read->windowHeight == SETTINGS.windowHeight);
    CHECK(read->fullscreen == SETTINGS.fullscreen);
    CHECK(read->language == SETTINGS.language);
    CHECK(read->combinedDamageNumbers == SETTINGS.combinedDamageNumbers);
    CHECK(read->damageNumberMotion == SETTINGS.damageNumberMotion);
    CHECK(read->textOutline == SETTINGS.textOutline);
    CHECK(read->textShadow == SETTINGS.textShadow);
}

TEST_CASE("missing settings give the defaults and write them out") {
    const Scratch scratch{ "missing" };
    const core::Settings settings = data::LoadSettings(scratch.File());
    CHECK(settings.windowWidth == core::Settings{}.windowWidth);
    REQUIRE(std::filesystem::exists(scratch.File()));
    const std::expected<core::Settings, core::Error> written = data::ParseSettings(Contents(scratch.File()));
    CHECK(written.has_value());
}

TEST_CASE("a malformed settings file gives the defaults and is left alone") {
    const Scratch scratch{ "malformed" };
    constexpr std::string_view BROKEN = "window: {width: 1920\n";
    Write(scratch.File(), BROKEN);
    const core::Settings settings = data::LoadSettings(scratch.File());
    CHECK(settings.windowWidth == core::Settings{}.windowWidth);
    const std::string contents = Contents(scratch.File());
    CHECK(contents == BROKEN);
}

} // namespace
