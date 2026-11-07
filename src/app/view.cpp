#include "app/view.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/profile.hpp"

namespace app {

namespace {

core::Vec2 Interpolated(core::Vec2 previous, core::Vec2 current, f64 alpha) {
    return previous + ((current - previous) * static_cast<f32>(alpha));
}

} // namespace

core::Vec2 FollowedCentre(const core::Follow& follow, f64 alpha) {
    PN_PROFILE_ZONE();
    return Interpolated(follow.previous, follow.current, alpha);
}

core::Vec2 WindowToWorld(const gfx::Camera& camera, core::Vec2 position, i32 width, i32 height) {
    PN_PROFILE_ZONE();
    const auto pixelsX = static_cast<f32>(std::max(width, 1));
    const auto pixelsY = static_cast<f32>(std::max(height, 1));
    const core::Vec2 clip{ .x = ((2.0F * position.x) / pixelsX) - 1.0F, .y = 1.0F - ((2.0F * position.y) / pixelsY) };
    return gfx::ClipToWorld(
        gfx::WorldToClip(camera, static_cast<u32>(std::max(width, 1)), static_cast<u32>(std::max(height, 1))), clip
    );
}

core::StableId Pick(const core::Snapshot& snapshot, f64 alpha, core::Vec2 world) {
    PN_PROFILE_ZONE();
    constexpr u32 LOW_HALF = 0xFFFFU;
    constexpr u32 HIGH_SHIFT = 16;
    const std::size_t count = std::min(snapshot.instances.size(), snapshot.ids.size());
    for (std::size_t i = count; i-- > 0;) {
        if (snapshot.ids[i] == core::StableId{}) {
            continue;
        }
        const core::Instance& instance = snapshot.instances[i];
        const core::Vec2 centre = Interpolated(instance.previous, instance.current, alpha);
        const f32 halfWidth = core::UnpackHalf(static_cast<u16>(instance.size & LOW_HALF)) / 2.0F;
        const f32 halfHeight = core::UnpackHalf(static_cast<u16>(instance.size >> HIGH_SHIFT)) / 2.0F;
        if (std::abs(world.x - centre.x) <= halfWidth && std::abs(world.y - centre.y) <= halfHeight) {
            return snapshot.ids[i];
        }
    }
    return {};
}

} // namespace app
