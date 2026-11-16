#include "app/click.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

namespace app {

core::Action ClickAction(bool worldMap, core::StableId picked, core::Vec2 worldPoint) {
    if (picked == core::StableId{} && worldMap) {
        return core::Action{ .kind = core::ActionKind::WalkTo, .point = worldPoint };
    }
    return core::Action{ .kind = core::ActionKind::Select, .target = picked };
}

} // namespace app
