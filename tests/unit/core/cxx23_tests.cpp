#include "core/cxx23.hpp"

#include <doctest/doctest.h>

#include <expected>

TEST_CASE("the toolchain provides C++23 library features")
{
    std::expected<int, const char*> value = 42;
    CHECK(value.has_value());
    CHECK(*value == 42);
}
