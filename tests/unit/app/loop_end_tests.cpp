#include "app/loop_end.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

#include <doctest/doctest.h>

namespace {

struct Case {
    bool failed;
    bool quit;
    bool quitAfter;
    app::LoopEnd expected;
};

TEST_CASE("a failure ends the loop as failed, whatever else asked to quit") {
    constexpr std::array CASES{
        Case{ .failed = true, .quit = false, .quitAfter = false, .expected = app::LoopEnd::Failed },
        Case{ .failed = true, .quit = true, .quitAfter = false, .expected = app::LoopEnd::Failed },
        Case{ .failed = true, .quit = false, .quitAfter = true, .expected = app::LoopEnd::Failed },
        Case{ .failed = true, .quit = true, .quitAfter = true, .expected = app::LoopEnd::Failed },
        Case{ .failed = false, .quit = true, .quitAfter = false, .expected = app::LoopEnd::Quit },
        Case{ .failed = false, .quit = false, .quitAfter = true, .expected = app::LoopEnd::Quit },
        Case{ .failed = false, .quit = true, .quitAfter = true, .expected = app::LoopEnd::Quit },
        Case{ .failed = false, .quit = false, .quitAfter = false, .expected = app::LoopEnd::None },
    };
    for (const Case& row : CASES) {
        CHECK(app::EndOf(row.failed, row.quit, row.quitAfter) == row.expected);
    }
}

} // namespace
