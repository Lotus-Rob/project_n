#include "game/scene.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep

namespace game {

SceneContext::~SceneContext() = default;

void Scene::
    Tick(SceneContext& /*context*/, const core::TickCounters& /*counters*/, std::vector<core::Event>& /*events*/) const {
}

void Scene::Respawn(SceneContext& /*context*/, const core::TickCounters& /*counters*/, u32 /*index*/) const {
}

void Scene::Publish(const SceneContext& /*context*/, core::Snapshot& /*snapshot*/) const {
}

} // namespace game
