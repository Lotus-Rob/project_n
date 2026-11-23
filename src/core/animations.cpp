#include "core/animations.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

namespace core {

const WalkClip* Animations::WalkOf(atlas::SpriteId standing) const {
    const auto found = std::ranges::find(walks, standing, &WalkClip::standing);
    return found == walks.end() ? nullptr : &*found;
}

atlas::SpriteId PoseSprite(const WalkClip* clip, atlas::SpriteId standing, f64 walked, bool moving) {
    if (!moving || clip == nullptr || clip->frames.empty()) {
        return standing;
    }
    PN_ASSERT(std::isfinite(clip->stride) && clip->stride > 0.0F, "a walk clip's stride is finite and above 0");
    const auto stride = static_cast<f64>(clip->stride);
    const f64 into = std::fmod(walked, static_cast<f64>(clip->frames.size()) * stride);
    const f64 last = static_cast<f64>(clip->frames.size() - 1);
    // An f32 stride times a frame count is exact in f64 and the division is correctly rounded, so no finite distance
    // reaches the frame past the last: the upper bound only keeps the index in range whatever the inputs. A negative or
    // NaN distance, which a caller should never pass, lands on the first frame.
    const f64 index = std::floor(into / stride);
    const f64 frame = index > 0.0 ? std::min(index, last) : 0.0;
    return clip->frames[static_cast<std::size_t>(frame)];
}

} // namespace core
