#include "core/scene_reports.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"

namespace core {

std::optional<Action> SceneReports::Answer(const SceneChange& change) {
    PN_PROFILE_ZONE();
    const bool answered = change.stage == m_stage && change.change == m_change;
    if ((change.stage != SceneStage::Leaving && change.stage != SceneStage::Entering) || answered) {
        return std::nullopt;
    }
    m_stage = change.stage;
    m_change = change.change;
    return Action{
        .kind = change.stage == SceneStage::Leaving ? ActionKind::SceneLeft : ActionKind::SceneEntered,
        .change = change.change,
    };
}

} // namespace core
