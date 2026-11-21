#include "gfx/route_draw.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/profile.hpp"

namespace gfx {

namespace {

constexpr std::size_t MARKS = 1; // the end marker, which the dots never crowd out

void Add(core::atlas::SpriteId sprite, core::Vec2 foot, const core::Manifest& manifest, TerrainDrawing& into) {
    into.instances.push_back(core::MakeInstance(manifest, { .sprite = sprite, .previous = foot, .current = foot }));
}

void AddDots(
    std::span<const core::Vec2> route,
    const core::RouteLook& look,
    const core::Manifest& manifest,
    TerrainDrawing& into
) {
    const std::size_t room = MAX_ROUTE_INSTANCES - MARKS;
    f32 travelled = 0.0F; // the length of the line before the segment being walked
    f32 next = 0.0F;      // where along the line the next dot falls
    for (std::size_t point = 1; point < route.size() && into.instances.size() < room; ++point) {
        const core::Vec2 from = route[point - 1];
        const core::Vec2 step = route[point] - from;
        const f32 length = std::hypot(step.x, step.y);
        if (length <= 0.0F) {
            continue;
        }
        while (next <= travelled + length && into.instances.size() < room) {
            const f32 along = (next - travelled) / length;
            Add(look.dot, from + (step * along), manifest, into);
            next += look.spacing;
        }
        travelled += length;
    }
}

// One normal batch over what was appended since `first`, none when nothing was.
void Close(std::size_t first, TerrainDrawing& into) {
    if (into.instances.size() > first) {
        into.batches.push_back(
            { .first = static_cast<u32>(first),
              .count = static_cast<u32>(into.instances.size() - first),
              .pipeline = core::Pipeline::Normal }
        );
    }
}

} // namespace

void DrawRoute(
    std::span<const core::Vec2> route,
    const core::RouteLook& look,
    const core::Manifest& manifest,
    TerrainDrawing& into
) {
    PN_PROFILE_ZONE();
    const std::size_t first = into.instances.size();
    if (route.size() >= 2 && look.spacing > 0.0F) {
        AddDots(route, look, manifest, into);
        Add(look.marker, route.back(), manifest, into);
    }
    Close(first, into);
}

void DrawTargetTile(
    std::optional<core::Vec2> cursorTile,
    const core::RouteLook& look,
    const core::Manifest& manifest,
    TerrainDrawing& into
) {
    PN_PROFILE_ZONE();
    const std::size_t first = into.instances.size();
    if (cursorTile) {
        Add(look.cursor, *cursorTile, manifest, into);
    }
    Close(first, into);
}

} // namespace gfx
