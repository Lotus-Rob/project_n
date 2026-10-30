#include "core/cxx23.hpp" // IWYU pragma: keep

#include <doctest/doctest.h>

#include <expected>

namespace {

constexpr int EXPECTED_VALUE = 42;

TEST_CASE("the toolchain provides C++23 library features") {
    std::expected<int, const char*> value = EXPECTED_VALUE;
    CHECK(value.has_value());
    CHECK(*value == EXPECTED_VALUE);
}

} // namespace
