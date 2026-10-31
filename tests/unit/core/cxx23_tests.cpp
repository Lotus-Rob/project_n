#include "core/cxx23.hpp" // IWYU pragma: keep

#include <doctest/doctest.h>

namespace {

constexpr i32 EXPECTED_VALUE = 42;

TEST_CASE("the toolchain provides C++23 library features") {
    std::expected<i32, const char*> value = EXPECTED_VALUE;
    CHECK(value.has_value());
    CHECK(*value == EXPECTED_VALUE);
}

} // namespace
