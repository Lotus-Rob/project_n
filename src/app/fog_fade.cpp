#include "app/fog_fade.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/fog.hpp"
#include "core/profile.hpp"

namespace app {

namespace {

constexpr f32 BYTE_RANGE = 255.0F;

u8 ToByte(f32 value) {
    return static_cast<u8>(std::lround(std::clamp(value, 0.0F, 1.0F) * BYTE_RANGE));
}

} // namespace

void FogFade::SetTargets(const core::Snapshot& snapshot, const core::WorldMap& map) {
    const core::FogView& fog = snapshot.fog;
    const std::size_t tiles = std::size_t{ map.Columns() } * map.Rows();
    m_target.resize(tiles * 2);
    m_moving = true;
    for (std::size_t tile = 0; tile < tiles; ++tile) {
        const auto index = static_cast<u32>(tile);
        m_target.at(tile * 2) = core::IsExplored(fog.explored, index) ? 1.0F : 0.0F;
        const bool inSight = fog.debugSight || (fog.playerTile && core::InSight(map, *fog.playerTile, index, fog.sightRadius));
        m_target.at((tile * 2) + 1) = inSight ? 1.0F : 0.0F;
    }
}

void FogFade::Snap() {
    m_current = m_target;
    m_bytes.resize(m_current.size());
    std::ranges::transform(m_current, m_bytes.begin(), ToByte);
    m_moving = false;
    ++m_revision;
}

void FogFade::Ease(f32 step) {
    bool changed = false;
    bool moving = false;
    for (std::size_t i = 0; i < m_current.size(); ++i) {
        f32& value = m_current[i];
        const f32 delta = m_target[i] - value;
        if (std::abs(delta) <= step) {
            value = m_target[i];
        }
        else {
            value += std::copysign(step, delta);
            moving = true;
        }
        const u8 byte = ToByte(value);
        if (byte != m_bytes[i]) {
            m_bytes[i] = byte;
            changed = true;
        }
    }
    m_moving = moving;
    if (changed) {
        ++m_revision;
    }
}

void FogFade::Update(const core::Snapshot& snapshot, const core::WorldMap& map, f32 seconds, f32 fadeSeconds) {
    PN_PROFILE_ZONE();
    PN_ASSERT(fadeSeconds > 0.0F, "the fog fades over some time");
    const core::FogView& fog = snapshot.fog;
    if (!fog.on) {
        m_on = false;
        m_seeded = false;
        return;
    }
    const Source source{
        .version = fog.version,
        .playerTile = fog.playerTile,
        .sightRadius = fog.sightRadius,
        .debugSight = fog.debugSight,
        .columns = map.Columns(),
        .rows = map.Rows(),
    };
    const bool snap = !m_seeded || snapshot.epoch != m_epoch || snapshot.sceneChange.change != m_change ||
                      source.debugSight != m_source.debugSight || source.columns != m_source.columns ||
                      source.rows != m_source.rows;
    m_on = true;
    m_columns = map.Columns();
    m_rows = map.Rows();
    m_tileSize = map.TileSize();
    if (snap || source != m_source) {
        m_source = source;
        SetTargets(snapshot, map);
    }
    if (snap) {
        m_seeded = true;
        m_epoch = snapshot.epoch;
        m_change = snapshot.sceneChange.change;
        Snap();
        return;
    }
    if (m_moving) {
        Ease(seconds / fadeSeconds);
    }
}

gfx::FogFrame FogFade::Frame() const {
    if (!m_on) {
        return {};
    }
    return { .columns = m_columns, .rows = m_rows, .tileSize = m_tileSize, .values = m_bytes, .revision = m_revision };
}

} // namespace app
