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
    CHECK(strings.Find("fox_name") == core::StringId::FoxName);
    CHECK(strings.Find("portal_label") == core::StringId::PortalLabel);
    CHECK_FALSE(strings.Find("nope").has_value());
}

TEST_CASE("an added string follows the named ones and is found by its key") {
    core::Strings strings;
    const core::StringId added = strings.Add("place_capital", { { "Azure Cloud City", "青云城" } });
    const core::StringId second = strings.Add("place_port", { { "Port", "港" } });
    CHECK(std::to_underlying(added) == core::STRINGS);
    CHECK(std::to_underlying(second) == core::STRINGS + 1);
    CHECK(strings.Find("place_capital") == added);
    CHECK(strings.Find("place_port") == second);
    CHECK(strings.Text(added, core::Language::Chinese) == "青云城");
    CHECK(strings.Text(second, core::Language::English) == "Port");
}

} // namespace
