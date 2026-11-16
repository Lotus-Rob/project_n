#include "core/cxx23.hpp" // IWYU pragma: keep
#include "data/names.hpp"
#include "platform/paths.hpp"

#include <doctest/doctest.h>

namespace {

constexpr std::string_view SMALL = R"yaml(surnames:
  - { zh: 王, pinyin: wang, weight: 90 }
  - { zh: 李, pinyin: li }
given:
  - { zh: 小, pinyin: xiao }
  - { zh: 明, pinyin: ming }
)yaml";

constexpr u16 WANG_WEIGHT = 90;
constexpr u32 FIRST_GIVEN_LINE = 5;
constexpr std::size_t LEAST_SURNAMES = 100;
constexpr std::size_t LEAST_GIVEN = 200;

// SMALL with `from` replaced by `replacement`.
std::string With(std::string_view from, std::string_view replacement) {
    std::string text{ SMALL };
    const std::size_t where = text.find(from);
    REQUIRE(where != std::string::npos);
    return text.replace(where, from.size(), replacement);
}

// The refusal of `text`, which must name `line` and `why`.
void CheckRefused(std::string_view text, u32 line, std::string_view why) {
    const std::expected<core::Names, core::Error> names = data::ParseNames(text);
    REQUIRE_FALSE(names.has_value());
    CHECK(names.error().message.contains(std::format("line {}", line)));
    CHECK(names.error().message.contains(why));
}

TEST_CASE("names.yaml reads into both tables") {
    const std::expected<core::Names, core::Error> names = data::ParseNames(SMALL);
    REQUIRE(names.has_value());
    REQUIRE(names->surnames.size() == 2);
    REQUIRE(names->given.size() == 2);
    CHECK(names->surnames[0].characters == "王");
    CHECK(names->surnames[0].pinyin == "wang");
    CHECK(names->surnames[0].weight == WANG_WEIGHT);
    CHECK(names->surnames[1].weight == 1);
    CHECK(names->given[1].characters == "明");
    CHECK(names->given[1].pinyin == "ming");
}

TEST_CASE("a names table it can't use is refused, naming its line") {
    CheckRefused("surnames: []\ngiven:\n  - { zh: 小, pinyin: xiao }\n", 1, "surnames");
    CheckRefused(With("{ zh: 李, pinyin: li }", "{ zh: \"\", pinyin: li }"), 3, "zh");
    CheckRefused(With("{ zh: 李, pinyin: li }", "{ zh: 李, pinyin: \"\" }"), 3, "pinyin");
    CheckRefused(With("{ zh: 李, pinyin: li }", "{ zh: 李 }"), 3, "pinyin");
    CheckRefused(With("pinyin: li", "pinyin: lǐ"), 3, "ASCII");
    CheckRefused(With("pinyin: li", "pinyin: Li"), 3, "lower-case");
    CheckRefused(With("weight: 90", "weight: 0"), 2, "weight");
    CheckRefused(With("weight: 90", "weight: 1.5"), 2, "weight");
    CheckRefused(With("{ zh: 小, pinyin: xiao }", "{ zh: 小, pinyin: xiao, weight: 2 }"), FIRST_GIVEN_LINE, "weight");
    CheckRefused("given:\n  - { zh: 小, pinyin: xiao }\n", 1, "surnames");
    CheckRefused("- a\n", 1, "map");
    CHECK_FALSE(data::ParseNames("surnames: [").has_value());
}

// A names table whose surnames list holds `count` entries.
std::string WithSurnames(std::size_t count) {
    std::string text = "surnames:\n";
    for (std::size_t index = 0; index < count; ++index) {
        text += "  - { zh: 王, pinyin: wang }\n";
    }
    return text + "given:\n  - { zh: 小, pinyin: xiao }\n";
}

TEST_CASE("a table past the most is refused, one of the most is read") {
    CheckRefused(WithSurnames(core::MOST_NAMES + 1), 2, "at most");
    const std::expected<core::Names, core::Error> full = data::ParseNames(WithSurnames(core::MOST_NAMES));
    REQUIRE(full.has_value());
    CHECK(full->surnames.size() == core::MOST_NAMES);
}

TEST_CASE("an unknown key is warned about, naming its line") {
    std::vector<std::string> warnings;
    const std::expected<core::Names, core::Error> names =
        data::ParseNames(With("{ zh: 李, pinyin: li }", "{ zh: 李, pinyin: li, colour: red }"), &warnings);
    REQUIRE(names.has_value());
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].contains("line 3"));
    CHECK(warnings[0].contains("colour"));
}

TEST_CASE("the shipped names.yaml reads") {
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    REQUIRE(assets.has_value());
    const std::expected<core::Names, core::Error> names = data::LoadNames(*assets / "data" / "names.yaml");
    REQUIRE(names.has_value());
    CHECK(names->surnames.size() >= LEAST_SURNAMES);
    CHECK(names->given.size() >= LEAST_GIVEN);
    CHECK_FALSE(data::LoadNames(*assets / "data" / "no_such_file.yaml").has_value());
}

} // namespace
