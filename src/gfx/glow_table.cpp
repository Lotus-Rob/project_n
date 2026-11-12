#include "gfx/glow_table.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

#include "core/tick_clock.hpp"

namespace gfx {

namespace {

GlowRecord RecordOf(const core::GlowLook& look, const core::UiColours& colours) {
    constexpr f32 TICKS = static_cast<f32>(core::TICKS_PER_SECOND);
    const core::GlowDrive& drive = look.drive;
    GlowRecord record{
        .size = look.size,
        .feather = look.feather,
        .opacity = look.opacity,
        .zoomShrink = look.zoomShrink,
        .blend = std::to_underlying(look.blend),
        .colour = look.colour.element ? 0U : colours.Of(look.colour.colour),
        .driveKind = std::to_underlying(drive.kind),
        .curve = std::to_underlying(drive.curve),
        .periodTicks = drive.periodSeconds * TICKS,
        .depth = drive.depth,
        .fadeTicks = drive.fadeSeconds * TICKS,
        .fadeCurve = std::to_underlying(drive.fadeCurve),
        .resultCount = drive.resultCount,
    };
    for (std::size_t result = 0; result < drive.resultCount; ++result) {
        record.results.at(result) = colours.Of(drive.results.at(result));
    }
    return record;
}

} // namespace

std::array<GlowRecord, core::GLOW_ROLES> BuildGlowTable(const core::Effects& effects, const core::UiColours& colours) {
    std::array<GlowRecord, core::GLOW_ROLES> table{};
    // None (role 0) stays zero: nothing is drawn for it.
    for (std::size_t role = 1; role < core::GLOW_ROLES; ++role) {
        table.at(role) = RecordOf(effects.glows.at(role), colours);
    }
    return table;
}

} // namespace gfx
