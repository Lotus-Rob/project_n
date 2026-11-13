#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/ui_colours.hpp"
#include "data/strings.hpp"
#include "platform/paths.hpp"

#include <doctest/doctest.h>

namespace {

constexpr std::string_view STRINGS = R"yaml(cultivator_name:
  en: Cultivator
  zh: 修士
tablet_caption:
  en: Ask the Way
  zh: 问道
fox_name:
  en: Fox
  zh: 狐
label_stack:
  en: "{name} (+{count})"
  zh: "{name}（+{count}）"
)yaml";

// STRINGS and every string the code names beyond them, each "x" in both languages: a table to which nothing is missing.
std::string Everything() {
    std::string text{ STRINGS };
    for (std::size_t string = 4; string < core::STRINGS; ++string) {
        text += std::format("{}:\n  en: x\n  zh: x\n", core::STRING_NAMES.at(string));
    }
    return text;
}

// Everything() with `from` replaced by `replacement`.
std::string With(std::string_view from, std::string_view replacement) {
    std::string text = Everything();
    const std::size_t where = text.find(from);
    REQUIRE(where != std::string::npos);
    return text.replace(where, from.size(), replacement);
}

std::string ErrorFor(const std::string& strings) {
    const std::expected<core::Strings, core::Error> parsed = data::ParseStrings(strings);
    REQUIRE_FALSE(parsed.has_value());
    return parsed.error().message;
}

TEST_CASE("strings parse into each language") {
    const std::expected<core::Strings, core::Error> strings = data::ParseStrings(Everything());
    REQUIRE(strings.has_value());
    CHECK(strings->Text(core::StringId::CultivatorName, core::Language::English) == "Cultivator");
    CHECK(strings->Text(core::StringId::CultivatorName, core::Language::Chinese) == "修士");
    CHECK(strings->Text(core::StringId::TabletCaption, core::Language::Chinese) == "问道");
}

TEST_CASE("an unknown string or language is reported with its line, and the rest still parse") {
    std::vector<std::string> warnings;
    const std::expected<core::Strings, core::Error> strings =
        data::ParseStrings(With("  zh: 问道\n", "  zh: 问道\n  fr: Demander\nfarewell:\n  en: Bye\n"), &warnings);
    REQUIRE(strings.has_value());
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0] == "strings line 7: tablet_caption has no language fr; it is ignored");
    CHECK(warnings[1] == "strings line 8: there is no string farewell; it is ignored");
}

// Each error is named before its CHECK: MSVC's C4866 refuses a temporary std::string inside one (tests/AGENTS.md).
TEST_CASE("a string the code names but the table lacks is an error") {
    const std::string error = ErrorFor(With("tablet_caption:\n  en: Ask the Way\n  zh: 问道\n", ""));
    CHECK(error == "strings line 1: there is no tablet_caption");
}

TEST_CASE("a string missing a language is an error naming its line") {
    const std::string error = ErrorFor(With("  zh: 修士\n", ""));
    CHECK(error == "strings line 2: cultivator_name has no zh");
}

TEST_CASE("a string given twice is an error naming the second") {
    const std::string error = ErrorFor(Everything() + "cultivator_name:\n  en: Again\n  zh: 再\n");
    CHECK(error == std::format("strings line {}: cultivator_name is there twice", 13 + (3 * (core::STRINGS - 4))));
}

TEST_CASE("a string's text that is empty or not text is an error naming its line") {
    const std::string empty = ErrorFor(With("en: Cultivator", "en: \"\""));
    const std::string list = ErrorFor(With("en: Cultivator", "en: [a, b]"));
    CHECK(empty == "strings line 2: cultivator_name's en isn't text");
    CHECK(list == "strings line 2: cultivator_name's en isn't text");
}

TEST_CASE("the strings table has the fox's name and the stack label in both languages") {
    const std::expected<core::Strings, core::Error> strings = data::ParseStrings(Everything());
    REQUIRE(strings.has_value());
    CHECK(strings->Text(core::StringId::FoxName, core::Language::English) == "Fox");
    CHECK(strings->Text(core::StringId::FoxName, core::Language::Chinese) == "狐");
    CHECK(strings->Text(core::StringId::LabelStack, core::Language::English) == "{name} (+{count})");
    CHECK(strings->Text(core::StringId::LabelStack, core::Language::Chinese) == "{name}（+{count}）");
}

TEST_CASE("the shipped strings table parses with no warning") {
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    REQUIRE(assets.has_value());
    std::vector<std::string> warnings;
    std::ifstream input{ *assets / "data" / "strings.yaml", std::ios::binary };
    const std::string text{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    const std::expected<core::Strings, core::Error> strings = data::ParseStrings(text, &warnings);
    REQUIRE(strings.has_value());
    CHECK(warnings.empty());
    CHECK(strings->Text(core::StringId::FoxName, core::Language::English) == "Fox");
    CHECK(strings->Text(core::StringId::FoxName, core::Language::Chinese) == "狐");
    CHECK(strings->Text(core::StringId::LabelStack, core::Language::English) == "{name} (+{count})");
    CHECK(strings->Text(core::StringId::LabelStack, core::Language::Chinese) == "{name}（+{count}）");
}

TEST_CASE("every area and colour name reads in both languages") {
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    REQUIRE(assets.has_value());
    std::ifstream input{ *assets / "data" / "strings.yaml", std::ios::binary };
    const std::string text{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    const std::expected<core::Strings, core::Error> strings = data::ParseStrings(text);
    REQUIRE(strings.has_value());
    for (const core::Language language : { core::Language::English, core::Language::Chinese }) {
        for (const core::ColourAreaInfo& area : core::COLOUR_AREA_INFO) {
            CHECK_FALSE(strings->Text(area.name, language).empty());
        }
        for (const core::UiColourInfo& colour : core::UI_COLOUR_INFO) {
            CHECK_FALSE(strings->Text(colour.name, language).empty());
        }
    }
    CHECK(strings->Text(core::StringId::ColourAreaHud, core::Language::English) == "HUD");
    CHECK(strings->Text(core::StringId::ColourQiBar, core::Language::Chinese) == "灵气条");
    CHECK(strings->Text(core::StringId::ColourSealGold, core::Language::English) == "Seal gold");
    CHECK(strings->Text(core::StringId::ColourSealGold, core::Language::Chinese) == "法印金");
    CHECK(strings->Text(core::StringId::ColourSealViolet, core::Language::English) == "Seal violet");
    CHECK(strings->Text(core::StringId::ColourSealViolet, core::Language::Chinese) == "法印紫");
}

TEST_CASE("the HUD strings fill their value") {
    constexpr i64 HEALTH = 300;
    constexpr i64 MOST = 400;
    constexpr i64 SECONDS = 2;
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    REQUIRE(assets.has_value());
    std::ifstream input{ *assets / "data" / "strings.yaml", std::ios::binary };
    const std::string text{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    const std::expected<core::Strings, core::Error> strings = data::ParseStrings(text);
    REQUIRE(strings.has_value());
    const auto filled = [&strings](core::StringId string, core::Language language, i64 value, i64 most) {
        std::string into = "left over";
        core::FillValue(into, strings->Text(string, language), value, most);
        return into;
    };
    const std::string health = filled(core::StringId::Health, core::Language::English, HEALTH, MOST);
    const std::string healthZh = filled(core::StringId::Health, core::Language::Chinese, HEALTH, MOST);
    const std::string qiText = filled(core::StringId::Qi, core::Language::English, HEALTH, MOST);
    const std::string qiZh = filled(core::StringId::Qi, core::Language::Chinese, HEALTH, MOST);
    const std::string cooldown = filled(core::StringId::Cooldown, core::Language::English, SECONDS, 0);
    const std::string cooldownZh = filled(core::StringId::Cooldown, core::Language::Chinese, SECONDS, 0);
    CHECK(health == "Health 300/400");
    CHECK(healthZh == "生命 300/400");
    CHECK(qiText == "Qi 300/400");
    CHECK(qiZh == "气 300/400");
    CHECK(cooldown == "2s");
    CHECK(cooldownZh == "2秒");
    CHECK(strings->Text(core::StringId::NotEnoughQi, core::Language::Chinese) == "气不足");
    CHECK(strings->Text(core::StringId::KeyRightClick, core::Language::English) == "Right click");
    CHECK(strings->Text(core::StringId::FlyingSwords, core::Language::Chinese) == "飞剑");
}

} // namespace
