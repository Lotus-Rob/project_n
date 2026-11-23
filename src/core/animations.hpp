#pragma once

/// @file
/// @ingroup core
/// Figures' animations: the walk clips that step a figure through its frames by how far it has walked. Presentation, not
/// game data: never hashed, saved or read by the simulation, so a figure's frame is a pure function of what it walked.

#include "core/types.hpp"

#include "core/generated/atlas.hpp" // by `project_n atlas`

#include <cstddef>
#include <vector>

namespace core {

/// The most frames a walk clip may have.
inline constexpr std::size_t MAX_WALK_FRAMES = 32;

/// A figure's walk: the frames it steps through in order while moving, side-on and facing right, each shown for `stride`
/// world units walked, the clip repeating once all have shown.
struct WalkClip {
    /// The sprite the figure stands in, which names the clip.
    atlas::SpriteId standing{};
    /// The frames in order, at least one and at most MAX_WALK_FRAMES.
    std::vector<atlas::SpriteId> frames;
    /// The world units walked while one frame shows: finite and above 0.
    f32 stride = 0.0F;
};

/// Every figure's animations.
struct Animations {
    /// The walk clips, one per standing sprite.
    std::vector<WalkClip> walks;

    /// The walk clip of the figure standing in `standing`, or null when it has none.
    [[nodiscard]] const WalkClip* WalkOf(atlas::SpriteId standing) const;
};

/// The sprite a figure standing in `standing` shows having walked `walked` world units: its standing sprite while it isn't
/// `moving` or has no `clip`, else the clip's frame `i` while `walked` modulo the clip's length (its frames times its
/// stride) lies in `[i × stride, (i + 1) × stride)`. A negative or NaN `walked` shows the first frame.
[[nodiscard]] atlas::SpriteId PoseSprite(const WalkClip* clip, atlas::SpriteId standing, f64 walked, bool moving);

} // namespace core
