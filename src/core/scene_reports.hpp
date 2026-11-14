#pragma once

/// @file
/// @ingroup core
/// The main thread's answers to the logic's scene changes (design doc 0002.3.1 §4, as built in phase 6 chunk 5): it
/// reports each change's way out and way in played, once each, and the logic moves the change on.

#include "core/frame.hpp"
#include "core/types.hpp"

#include <optional>

namespace core {

/// What the main thread answers for the scene change of each snapshot it draws (Snapshot::sceneChange): at once for now,
/// as nothing draws the ways out and in yet.
class SceneReports {
public:
    /// The report to send for `change`: a SceneLeft while it leaves, a SceneEntered while it enters, each naming the
    /// change; once per stage of each change, so none for a stage already answered, and none while running or loading.
    [[nodiscard]] std::optional<Action> Answer(const SceneChange& change);

private:
    SceneStage m_stage = SceneStage::Running; // the stage answered last
    u64 m_change = 0;                         // the change it belongs to
};

} // namespace core
