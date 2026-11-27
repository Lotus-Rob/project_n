#include "core/failed_frames.hpp"
#include "core/tick_clock.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 START_NS = 7 * core::NS_PER_SECOND; // an arbitrary clock reading: the run's start is read from the first call
constexpr u64 SECOND = core::NS_PER_SECOND;
constexpr u64 TENTH = SECOND / 10;

struct Reading {
    std::string_view why;
    u64 failures;
    u64 sinceStartNs;
    bool lost;
};

TEST_CASE("a run is lost only once it is both long in frames and in time") {
    constexpr auto READINGS = std::to_array<Reading>({
        { .why = "one frame short", .failures = 119, .sinceStartNs = 10 * SECOND, .lost = false },
        { .why = "a tenth of a second short", .failures = 120, .sinceStartNs = (29 * TENTH), .lost = false },
        { .why = "both bounds met", .failures = 120, .sinceStartNs = 3 * SECOND, .lost = true },
        { .why = "unpaced: many frames, little time", .failures = 5000, .sinceStartNs = TENTH * 5, .lost = false },
    });
    for (const Reading& reading : READINGS) {
        CAPTURE(reading.why);
        core::FailedFrames frames;
        static_cast<void>(frames.Lost(1, START_NS)); // the run begins
        CHECK(frames.Lost(reading.failures, START_NS + reading.sinceStartNs) == reading.lost);
    }
}

TEST_CASE("a success in between starts the run again") {
    core::FailedFrames frames;
    CHECK_FALSE(frames.Lost(100, START_NS));
    CHECK_FALSE(frames.Lost(0, START_NS + SECOND));
    CHECK_FALSE(frames.Lost(120, START_NS + (35 * TENTH))); // the second run begins here
    CHECK_FALSE(frames.Lost(400, START_NS + (64 * TENTH)));
    CHECK(frames.Lost(500, START_NS + (65 * TENTH)));
}

TEST_CASE("a frame that can't be judged ends the run") {
    core::FailedFrames frames;
    CHECK_FALSE(frames.Lost(119, START_NS));
    frames.Unjudged();                                       // the window was minimised
    CHECK_FALSE(frames.Lost(120, START_NS + (10 * SECOND))); // the first failure after the restore begins a run
    CHECK_FALSE(frames.Lost(200, START_NS + (12 * SECOND)));
    CHECK(frames.Lost(400, START_NS + (13 * SECOND)));
}

TEST_CASE("a run lasts from its first failed frame") {
    core::FailedFrames frames;
    CHECK(frames.RunNs(START_NS) == 0);
    static_cast<void>(frames.Lost(1, START_NS));
    CHECK(frames.RunNs(START_NS + SECOND) == SECOND);
}

} // namespace
