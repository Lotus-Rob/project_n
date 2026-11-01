#include "core/repeated_failure.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 FAILURES_IN_RUN = 3;

TEST_CASE("only the first failure of a run is logged") {
    core::RepeatedFailure failures;
    CHECK(failures.Fail());
    CHECK_FALSE(failures.Fail());
    CHECK_FALSE(failures.Fail());
}

TEST_CASE("a success reports how many failures in a row it ended") {
    core::RepeatedFailure failures;
    for (u64 i = 0; i < FAILURES_IN_RUN; ++i) {
        static_cast<void>(failures.Fail());
    }
    CHECK(failures.Succeed() == FAILURES_IN_RUN);
}

TEST_CASE("a failure after a success starts a new run, which is logged") {
    core::RepeatedFailure failures;
    static_cast<void>(failures.Fail());
    static_cast<void>(failures.Succeed());
    CHECK(failures.Fail());
}

TEST_CASE("a success after no failures reports none") {
    core::RepeatedFailure failures;
    CHECK(failures.Succeed() == 0);
}

} // namespace
