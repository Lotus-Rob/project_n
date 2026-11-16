#pragma once

/// @file
/// @ingroup game
/// The world map: a stand-in about 20 screens across at zoom 1, for proving the camera, culling and
/// the measurements on a map too big to see at once. Its terrain is no scene content: the renderer draws the map
/// (`assets/data/world.yaml`) from its data and look (SceneTraits::worldMap). A crowd of
/// walkers, each circling its own point and denser in a few gathering areas, wanders the whole map, every eighth
/// with a name label; they are plain entities (a body, a look and an orbit), so a save holds them as any scene's and
/// where each stands is a function of its orbit and the tick. The cultivator walks it, the camera following until it
/// is panned (core::CameraKind::Roam). The walkers are a stand-in for the travellers the world map will carry.

#include "core/frame.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "core/world_map.hpp"
#include "game/scene.hpp"

#include <optional>
#include <vector>

namespace game {

/// The side of one terrain tile, in world units.
inline constexpr f32 WORLD_TILE = 256.0F;
/// How many columns of tiles the map has.
inline constexpr u32 WORLD_COLUMNS = 150;
/// How many rows of tiles the map has: the last reaches a little past the map's bottom edge.
inline constexpr u32 WORLD_ROWS = 85;
/// The world map: 38,400 by 21,600 world units, 20 screens of 1,920 by 1,080 across and as many down.
inline constexpr core::Rect WORLD_MAP{ .min = {}, .max = { .x = 38'400.0F, .y = 21'600.0F } };
/// The middle of the map's busiest gathering, in the east and clear of the start: the widest view centred here
/// holds thousands of walkers, more than anywhere else.
inline constexpr core::Vec2 WORLD_BUSIEST{ .x = 33'000.0F, .y = 10'800.0F };

/// Where the portal stands from the start city's tile centre, in world units: up and to the left, on the first screen, in
/// the quiet country round the start, west of it and off the strip east of the start that `--camera-path` walks along.
inline constexpr core::Vec2 WORLD_PORTAL_OFFSET{ .x = -560.0F, .y = -200.0F };

/// How many walkers the spread crowd has before the gatherings' (the first walker indices): the gathered walkers' draws
/// are keyed on the index after these, in the order of WorldGatherings().
inline constexpr u32 WORLD_SPREAD_WALKERS = 1'600;

/// One gathering of the stand-in crowd: an area of walkers laid on a grid of cells, one walker each, round a city.
struct Gathering {
    /// The area's middle: the centre of its city's tile.
    core::Vec2 point{};
    /// Half the area's width and height.
    core::Vec2 half{};
    /// Cells across.
    u32 columns = 0;
    /// Cells down; one walker each.
    u32 rows = 0;
};

/// The gatherings `map` gives the world scene: the busiest round the city whose tile holds WORLD_BUSIEST (the port), then
/// three towns round the first three other cities in place order, the start city and the busiest's excluded; fewer when
/// the map has fewer cities. A map change moves them with its cities.
[[nodiscard]] std::vector<Gathering> WorldGatherings(const core::WorldMap& map);

/// The area `gathering`'s walkers' feet can reach: its own, grown by the orbits' reach.
[[nodiscard]] core::Rect GatheringReach(const Gathering& gathering);

/// Where the `walker`th walker of the spread crowd would circle on its `redraw`th try (0 the first), before the map says
/// whether the tile is passable.
[[nodiscard]] core::Vec2 SpreadDraw(u64 seed, u32 walker, u32 redraw);

/// Where the `walker`th walker of the spread crowd circles under `seed`: on the map, away from its edges, on a passable
/// tile. A centre that lands on an impassable one is redrawn with the next draw index, up to 16 times; none then.
[[nodiscard]] std::optional<core::Vec2> SpreadCentre(const core::WorldMap& map, u64 seed, u32 walker);

/// Where the `walker`th walker, the `member`th of `gathering`, circles under `seed`: on a passable tile of its area. Its cell's
/// jittered centre when that is passable; else redrawn with the next draw indices anywhere in the area, up to 16 times;
/// none then.
[[nodiscard]] std::optional<core::Vec2> GatheredCentre(
    const core::WorldMap& map,
    u64 seed,
    u32 walker,
    u32 member,
    const Gathering& gathering
);

/// The world map's scene: wandering walkers, the cultivator walking it, over the map the renderer draws.
class WorldScene final : public Scene {
public:
    /// Named `world`, viewing the whole map, with creatures (the cultivator) and no grid of world NPCs (`worldNpcs` 0: Build
    /// spawns the cities' people on the map instead), the camera following the cultivator until panned, orbits returning to
    /// the map's centre, and its portal a short walk from the start, on the first screen.
    [[nodiscard]] const SceneTraits& Traits() const override;

    /// The start city's tile centre plus WORLD_PORTAL_OFFSET; none without a map.
    [[nodiscard]] std::optional<core::Vec2> Portal(const SceneContext& context) const override;

    /// The cultivator on the start city's tile centre, made the player; each city's `people` as world NPCs homed in it
    /// (seven tenths on the city's tile, the rest on drawn tiles of its region); then about 5,800 walkers (5,845 less those the
    /// sea leaves out): 1,600 spread evenly over the whole map (about 4 a screen; any whose centre would be on an impassable
    /// tile are redrawn or left out) and the rest evenly over the WorldGatherings(), none within the widest view of the start:
    /// the busiest, 3,120 walkers (about 130 a screen, those whose spot is sea redrawn on to land or left out) and three towns
    /// of 375 (about 60 a screen). Each circles a point of its own at a walking pace, its orbit drawn from core/random.hpp
    /// keyed on its index. Last, the people set out on the visits a month of travel would have them on
    /// (SceneContext::SeedTravel()), so the roads are busy from the first frame.
    void Build(SceneContext& context) const override;

    /// A name label over every eighth walker (by its ID) standing within `view`, and the name of every place of the
    /// map whose tile centre lies within it (core::TextKind::Place), hung under the tile's marker so a cultivator standing
    /// on a city never has its own label over the name.
    void Publish(const SceneContext& context, core::Rect view, core::Snapshot& snapshot) const override;
};

} // namespace game
