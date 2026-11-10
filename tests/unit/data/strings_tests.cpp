#include "core/cxx23.hpp" // IWYU pragma: keep
#include "data/strings.hpp"
#include "platform/paths.hpp"

#include <doctest/doctest.h>

namespace {

constexpr std::string_view STRINGS = R"(cultivator_name:
  en: Cultivator
  zh: 修士
tablet_caption:
  en: Ask the Way
  zh: 问道
)";

// STRINGS with `from` replaced by `replacement`.
std::string With(std::string_view from, std::string_view replacement) {
    std::string text{ STRINGS };
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
    const std::expected<core::Strings, core::Error> strings = data::ParseStrings(STRINGS);
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
    const std::string error = ErrorFor(std::string{ STRINGS } + "cultivator_name:\n  en: Again\n  zh: 再\n");
    CHECK(error == "strings line 7: cultivator_name is there twice");
}

TEST_CASE("a string's text that is empty or not text is an error naming its line") {
    const std::string empty = ErrorFor(With("en: Cultivator", "en: \"\""));
    const std::string list = ErrorFor(With("en: Cultivator", "en: [a, b]"));
    CHECK(empty == "strings line 2: cultivator_name's en isn't text");
    CHECK(list == "strings line 2: cultivator_name's en isn't text");
}

TEST_CASE("the shipped strings table parses with no warning") {
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    REQUIRE(assets.has_value());
    std::vector<std::string> warnings;
    std::ifstream input{ *assets / "data" / "strings.yaml", std::ios::binary };
    const std::string text{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    CHECK(data::ParseStrings(text, &warnings).has_value());
    CHECK(warnings.empty());
}

} // namespace
