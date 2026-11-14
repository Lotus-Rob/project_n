#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/scene_reports.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u64 SECOND = 2;
constexpr u64 THIRD = 3;

// What a report says: its kind and the change it names.
using Report = std::pair<core::ActionKind, u64>;

// What `reports` answers for change `change` at `stage`, if anything.
std::optional<Report> Answered(core::SceneReports& reports, core::SceneStage stage, u64 change) {
    const std::optional<core::Action> answer = reports.Answer({ .stage = stage, .change = change });
    if (!answer) {
        return std::nullopt;
    }
    return Report{ answer->kind, answer->change };
}

TEST_CASE("answers each stage once") {
    using enum core::SceneStage;
    constexpr Report LEFT_SECOND{ core::ActionKind::SceneLeft, SECOND };
    constexpr Report ENTERED_SECOND{ core::ActionKind::SceneEntered, SECOND };
    constexpr Report LEFT_THIRD{ core::ActionKind::SceneLeft, THIRD };
    core::SceneReports reports;
    const std::optional<Report> left = Answered(reports, Leaving, SECOND);
    const std::optional<Report> leftAgain = Answered(reports, Leaving, SECOND); // the same snapshot drawn again
    const std::optional<Report> loading = Answered(reports, Loading, SECOND);
    const std::optional<Report> entered = Answered(reports, Entering, SECOND);
    const std::optional<Report> enteredAgain = Answered(reports, Entering, SECOND);
    const std::optional<Report> running = Answered(reports, Running, SECOND);
    const std::optional<Report> next = Answered(reports, Leaving, THIRD);
    CHECK(left == LEFT_SECOND);
    CHECK_FALSE(leftAgain.has_value());
    CHECK_FALSE(loading.has_value());
    CHECK(entered == ENTERED_SECOND);
    CHECK_FALSE(enteredAgain.has_value());
    CHECK_FALSE(running.has_value());
    CHECK(next == LEFT_THIRD);
}

TEST_CASE("the start's entering is answered, as change 1") {
    constexpr Report ENTERED_FIRST{ core::ActionKind::SceneEntered, 1 };
    core::SceneReports reports;
    const std::optional<Report> loading = Answered(reports, core::SceneStage::Loading, 1);
    const std::optional<Report> entered = Answered(reports, core::SceneStage::Entering, 1);
    CHECK_FALSE(loading.has_value());
    CHECK(entered == ENTERED_FIRST);
}

} // namespace
