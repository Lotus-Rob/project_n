#include "gfx/road_draw.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"
#include "gfx/ribbon_band.hpp"

namespace gfx {

void DrawRoads(std::span<const std::vector<core::Vec2>> curves, const RoadShape& shape, std::vector<RibbonVertex>& into) {
    PN_PROFILE_ZONE();
    into.clear();
    std::vector<Laid> lines;
    std::size_t quads = 0;
    for (std::size_t index = 0; index < curves.size(); ++index) {
        const std::vector<core::Vec2>& curve = curves[index];
        if (curve.size() < 2) {
            continue;
        }
        const std::size_t taken = 2 * (curve.size() - 1);
        if (quads + taken > MAX_ROAD_QUADS) {
            PN_LOG_WARNING(
                "{} of {} roads left out: drawing them takes more than {} quads",
                curves.size() - index,
                curves.size(),
                MAX_ROAD_QUADS
            );
            break;
        }
        quads += taken;
        lines.push_back({ .line = curve });
    }
    into.reserve(quads * QUAD_VERTICES);
    for (const Laid& laid : lines) {
        AddBand(laid, shape.edgeWidth, shape.edgeColour, into);
    }
    for (const Laid& laid : lines) {
        AddBand(laid, shape.coreWidth, shape.coreColour, into);
    }
}

} // namespace gfx
