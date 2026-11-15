#include "game/scene.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/generated/atlas.hpp"
#include "core/instances.hpp"
#include "core/profile.hpp"

namespace game {

SceneContext::~SceneContext() = default;

void Scene::
    Tick(SceneContext& /*context*/, const core::TickCounters& /*counters*/, std::vector<core::Event>& /*events*/) const {
}

std::optional<core::Vec2> Scene::Portal(const SceneContext& /*context*/) const {
    return Traits().portal;
}

void Scene::Respawn(SceneContext& /*context*/, const core::TickCounters& /*counters*/, u32 /*index*/) const {
}

void Scene::PublishGround(const SceneContext& context, core::Rect /*view*/, core::DrawList& drawList, u32 part) const {
    PN_PROFILE_ZONE();
    if (Traits().worldMap) {
        return;
    }
    // Whole: its quad is far larger than the cull margin, so its centre may lie outside the view while part of it shows.
    drawList.AddWhole(
        part,
        core::Layer::Ground,
        core::Pipeline::Normal,
        core::MakeInstance(context.Manifest(), { .sprite = core::atlas::SpriteId::Ground })
    );
}

void Scene::Publish(const SceneContext& /*context*/, core::Rect /*view*/, core::Snapshot& /*snapshot*/) const {
}

} // namespace game
