#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/strings.hpp"

#include <doctest/doctest.h>

namespace {

TEST_CASE("a default table holds every named string, empty, and no data string") {
    const core::Strings strings;
    CHECK(strings.text.size() == core::STRINGS);
    CHECK(strings.keys.empty());
    CHECK(strings.Text(core::StringId::FoxName, core::Language::English).empty());
}

TEST_CASE("a named string is found by its key") {
    const core::Strings strings;
    const std::optional<core::StringId> fox = strings.Find("fox_name");
    const std::optional<core::StringId> portal = strings.Find("portal_label");
    const std::optional<core::StringId> nope = strings.Find("nope");
    CHECK(fox == core::StringId::FoxName);
    CHECK(portal == core::StringId::PortalLabel);
    CHECK_FALSE(nope.has_value());
}

TEST_CASE("an added string follows the named ones and is found by its key") {
    core::Strings strings;
    const core::StringId added = strings.Add("place_capital", { { "Azure Cloud City", "青云城" } });
    const core::StringId second = strings.Add("place_port", { { "Port", "港" } });
    CHECK(std::to_underlying(added) == core::STRINGS);
    CHECK(std::to_underlying(second) == core::STRINGS + 1);
    const std::optional<core::StringId> capital = strings.Find("place_capital");
    const std::optional<core::StringId> port = strings.Find("place_port");
    CHECK(capital == added);
    CHECK(port == second);
    CHECK(strings.Text(added, core::Language::Chinese) == "青云城");
    CHECK(strings.Text(second, core::Language::English) == "Port");
}

TEST_CASE("the last string a table holds gets the last id") {
    core::Strings strings;
    strings.text.resize(core::MOST_STRINGS - 1);
    strings.keys.resize(core::MOST_STRINGS - 1 - core::STRINGS);
    const core::StringId last = strings.Add("last", { { "a", "b" } });
    CHECK(std::to_underlying(last) == core::MOST_STRINGS - 1);
    const std::optional<core::StringId> found = strings.Find("last");
    CHECK(found == last);
    CHECK(strings.text.size() == core::MOST_STRINGS);
}

} // namespace
