#pragma once

/// @file
/// @ingroup app
/// The world map's fog as the main thread draws it: each tile's explored and in-sight values ease towards what the
/// snapshot says, so a newly seen tile and a sight that moves fade rather than jump. Window-free: tested with constructed
/// snapshots.

#include "core/frame.hpp"
#include "core/types.hpp"
#include "core/world_map.hpp"
#include "gfx/fog.hpp"

#include <optional>
#include <vector>

namespace app {

/// The eased fog values of the world map's tiles.
class FogFade {
public:
    /// Moves every value `seconds` closer to its target from `snapshot`'s fog on `map`, a full range taking `fadeSeconds`
    /// (linearly: the shaders shape it). Nothing is touched while no value moves. The values snap to their targets on the
    /// first fog seen, a new epoch, a new scene change and when the debug sight switches. `fadeSeconds` must be positive.
    void Update(const core::Snapshot& snapshot, const core::WorldMap& map, f32 seconds, f32 fadeSeconds);

    /// The values to draw: no columns while the snapshot had no fog. Valid until the next Update().
    [[nodiscard]] gfx::FogFrame Frame() const;

private:
    /// What the targets were computed from; they are recomputed only when it changes.
    struct Source {
        u64 version = 0;
        std::optional<u32> playerTile = std::nullopt;
        u32 sightRadius = 0;
        bool debugSight = false;
        u32 columns = 0;
        u32 rows = 0;

        [[nodiscard]] friend bool operator==(const Source& lhs, const Source& rhs) = default;
    };

    void SetTargets(const core::Snapshot& snapshot, const core::WorldMap& map);
    void Snap();
    void Ease(f32 step);

    bool m_on = false;
    bool m_seeded = false;
    bool m_moving = false;
    u64 m_epoch = 0;
    u64 m_change = 0;
    Source m_source{};
    u32 m_columns = 0;
    u32 m_rows = 0;
    f32 m_tileSize = 0.0F;
    u64 m_revision = 0;
    std::vector<f32> m_current;
    std::vector<f32> m_target;
    std::vector<u8> m_bytes;
};

} // namespace app
