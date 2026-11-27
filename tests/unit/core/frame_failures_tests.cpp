#include "core/frame_failures.hpp"

#include <doctest/doctest.h>

namespace {

core::Error Because(std::string why) {
    return core::Error{ std::move(why) };
}

TEST_CASE("a run keeps the first error and logs only that one") {
    core::FrameFailures run;
    CHECK(run.First().message.empty());
    CHECK(run.Fail(Because("no swapchain")));
    CHECK_FALSE(run.Fail(Because("device lost")));
    CHECK(run.First().message == "no swapchain");
    CHECK(run.Failures() == 2);
}

TEST_CASE("ending a run reports its length and forgets its error") {
    core::FrameFailures run;
    static_cast<void>(run.Fail(Because("no swapchain")));
    static_cast<void>(run.Fail(Because("no swapchain")));
    CHECK(run.End() == 2);
    CHECK(run.Failures() == 0);
    CHECK(run.First().message.empty());
}

TEST_CASE("a run after an ended one starts with its own first error") {
    core::FrameFailures run;
    static_cast<void>(run.Fail(Because("first run")));
    static_cast<void>(run.End());
    CHECK(run.Fail(Because("second run")));
    CHECK(run.First().message == "second run");
}

TEST_CASE("ending when nothing failed reports none") {
    core::FrameFailures run;
    CHECK(run.End() == 0);
}

} // namespace
