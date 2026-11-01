#include "core/cxx23.hpp" // IWYU pragma: keep
#include "platform/paths.hpp"

#include <doctest/doctest.h>

namespace {

#ifdef _WIN32
constexpr std::string_view EXECUTABLE = "platform_tests.exe";
#else
constexpr std::string_view EXECUTABLE = "platform_tests";
#endif

TEST_CASE("the assets folder is the one beside the executable") {
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    REQUIRE(assets.has_value());
    CHECK(assets->is_absolute());
    CHECK(assets->filename() == "assets");
    CHECK(std::filesystem::is_regular_file(assets->parent_path() / EXECUTABLE));
}

} // namespace
