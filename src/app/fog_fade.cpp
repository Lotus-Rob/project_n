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
    m_target.assign(tiles * 2, 0.0F);
    for (std::size_t tile = 0; tile < tiles; ++tile) {
        m_target[tile * 2] = core::IsExplored(fog.explored, static_cast<u32>(tile)) ? 1.0F : 0.0F;
    }
    if (fog.debugSight) {
        for (std::size_t tile = 0; tile < tiles; ++tile) {
            m_target[(tile * 2) + 1] = 1.0F;
        }
    }
    else if (fog.playerTile) {
        core::ForEachInSight(map, *fog.playerTile, fog.sightRadius, [this](u32 tile) {
            m_target[(std::size_t{ tile } * 2) + 1] = 1.0F;
        });
    }
}

void FogFade::FindMovers() {
    m_movers.clear();
    for (std::size_t i = 0; i < m_current.size(); ++i) {
        if (std::abs(m_target[i] - m_current[i]) > 0.0F) {
            m_movers.push_back(static_cast<u32>(i));
        }
    }
}

void FogFade::Snap() {
    m_current = m_target;
    m_bytes.resize(m_current.size());
    std::ranges::transform(m_current, m_bytes.begin(), ToByte);
    m_movers.clear();
    ++m_revision;
}

void FogFade::Ease(f32 step) {
    bool changed = false;
    std::size_t kept = 0;
    for (const u32 index : m_movers) {
        f32& value = m_current[index];
        const f32 delta = m_target[index] - value;
        if (std::abs(delta) <= step) {
            value = m_target[index];
        }
        else {
            value += std::copysign(step, delta);
            m_movers[kept++] = index;
        }
        const u8 byte = ToByte(value);
        if (byte != m_bytes[index]) {
            m_bytes[index] = byte;
            changed = true;
        }
    }
    m_movers.resize(kept);
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
        if (!snap) {
            FindMovers();
        }
    }
    if (snap) {
        m_seeded = true;
        m_epoch = snapshot.epoch;
        m_change = snapshot.sceneChange.change;
        Snap();
        return;
    }
    if (!m_movers.empty()) {
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
