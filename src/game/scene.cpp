#include "game/scene.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace game {

SceneContext::~SceneContext() = default;

void SceneClass::
    Tick(SceneContext& /*context*/, const core::TickCounters& /*counters*/, std::vector<core::Event>& /*events*/) const {
}

void SceneClass::Respawn(SceneContext& /*context*/, const core::TickCounters& /*counters*/, u32 /*index*/) const {
}

void SceneClass::Publish(const SceneContext& /*context*/, core::Snapshot& /*snapshot*/) const {
}

core::Vec2 HeadOf(const core::Registry& entities, const core::Manifest& manifest, core::Entity entity) {
    const core::Vec2 foot = entities.get<Body>(entity).current;
    const Look& look = entities.get<Look>(entity);
    const f32 footY = static_cast<f32>(manifest.Sprite(look.sprite).footY);
    return { .x = foot.x, .y = foot.y - (footY * look.scale) };
}

} // namespace game
