#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 TARGET_NS = 16'000'000;
constexpr u64 ELAPSED_NS = 4'000'000;

TEST_CASE("a frame shorter than the target waits out the rest") {
    CHECK(core::FrameWait(ELAPSED_NS, TARGET_NS) == TARGET_NS - ELAPSED_NS);
}

TEST_CASE("a frame longer than the target doesn't wait") {
    CHECK(core::FrameWait(TARGET_NS + ELAPSED_NS, TARGET_NS) == 0);
}

} // namespace
