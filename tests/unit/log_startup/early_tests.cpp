#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"

#include <doctest/doctest.h>

namespace {

// Its own executable: Quill's loggers live for the whole process, and nothing here calls Start().
TEST_CASE("before Start, log calls go to the console-only early logger") {
    CHECK(core::log::Root()->get_logger_name() == "early");
}

} // namespace
