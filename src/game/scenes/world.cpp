#include "game/scenes/world.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"
#include "core/random.hpp"
#include "game/npcs.hpp"
#include "game/random_purposes.hpp"

namespace game {

namespace {

using core::atlas::SpriteId;

constexpr f32 WORLD_HALF = 0.5F;
constexpr core::Vec2 WORLD_CENTRE = (WORLD_MAP.min + WORLD_MAP.max) * WORLD_HALF;

constexpr SceneTraits WORLD_TRAITS{
    .name = "world",
    .view = WORLD_MAP,
    .home = WORLD_CENTRE,
    .creatures = true,
    .camera = core::CameraKind::Roam,
    .worldMap = true,
    .portalFromMap = true,
};

static_assert(
    WORLD_COLUMNS * static_cast<u32>(WORLD_TILE) == static_cast<u32>(WORLD_MAP.max.x),
    "the columns cover the map exactly"
);
static_assert(
    static_cast<f32>(WORLD_ROWS - 1) * WORLD_TILE < WORLD_MAP.max.y &&
        static_cast<f32>(WORLD_ROWS) * WORLD_TILE >= WORLD_MAP.max.y,
    "the last row reaches the map's bottom edge"
);

constexpr f32 ORBIT_RADIUS_MIN = 120.0F;
constexpr f32 ORBIT_RADIUS_RANGE = 480.0F;
constexpr f32 WOBBLE_MIN = 0.1F; // of the radius
constexpr f32 WOBBLE_RANGE = 0.3F;
constexpr f32 PACE_MIN = 0.5F; // world units a tick: 30 to 90 a second, a stroll beside the cultivator's 240
constexpr f32 PACE_RANGE = 1.0F;
// The farthest a walker's foot strays from its orbit's centre: the largest radius plus its largest wobble (OrbitPoint()
// adds the wobble to the radius, and tilts only y, so x reaches this far).
constexpr f32 ORBIT_REACH = (ORBIT_RADIUS_MIN + ORBIT_RADIUS_RANGE) * (1.0F + WOBBLE_MIN + WOBBLE_RANGE);
constexpr u32 LABELLED_EVERY = 8;

// The walkers: a thin crowd spread over the whole map, its orbits' centres kept ORBIT_REACH from the edges, and denser
// crowds in gathering areas. Both are laid on a grid of cells, one walker's centre jittered within each, so the crowd
// is even at every scale: a screen holds about the same count wherever it lies in the area, none a pile.
// The busiest gathering fills a 7,680 by 4,320 view round WORLD_BUSIEST and a margin for the orbits' reach:
// 3,120 walkers on 8,880 by 5,520 units, about 130 a screen, those whose spot is sea redrawn on to land (1,920 by 1,080); the
// orbits' scatter makes a screen's count vary by about 20 either way, so any denser would pile up past 150 and any thinner
// leave the view short of 2,000. These counts depend on the walker draws, their purposes and the orbit ranges: change one and
// retune the density, never the tests. The towns are 4,800 by 2,700: 375 walkers each, about 60 a screen. All, their walkers'
// orbits included, stand clear of the widest view round the start, which is 15,360 by 8,640, and of each other (the shipped
// map's tests check both).
constexpr Gathering BUSIEST{ .point = {}, .half = { .x = 4'440.0F, .y = 2'760.0F }, .columns = 60, .rows = 52 };
constexpr Gathering TOWN{ .point = {}, .half = { .x = 2'400.0F, .y = 1'350.0F }, .columns = 25, .rows = 15 };
constexpr u32 TOWNS = 3;
// The spread crowd: 50 by 32 cells over the map less its margins, about 4 walkers a screen.
constexpr u32 SPREAD_COLUMNS = 50;
constexpr u32 SPREAD_ROWS = 32;
constexpr u32 SPREAD_WALKERS = SPREAD_COLUMNS * SPREAD_ROWS;
static_assert(SPREAD_WALKERS == WORLD_SPREAD_WALKERS, "the header says how many walkers the spread crowd has");
// How often a spread walker redraws its centre when it lands on an impassable tile.
constexpr u32 SPREAD_REDRAWS = 16;
// And a gathered walker's.
constexpr u32 GATHERED_REDRAWS = 16;

// The numbers each walker draws (core::Random's draw index).
constexpr u32 DRAW_CENTRE_X = 0; // the jitter within its cell: this draw and the next (x then y)
constexpr u32 DRAW_RADIUS = 2;
constexpr u32 DRAW_WOBBLE = 3;
constexpr u32 DRAW_PHASE = 4;
constexpr u32 DRAW_PACE = 5;
constexpr u32 DRAW_DIRECTION = 6;
constexpr u32 DRAW_REDRAW = 7; // a redraw's jitter: the next pair of draws after these, two to a redraw

/// A walker's random number `draw`, as a value in [0, 1).
f32 WalkerUnit(u64 seed, u32 walker, u32 draw) {
    return static_cast<f32>(core::ToUnit(core::Random(seed, 0, walker, PURPOSE_WALKER, draw)));
}

/// The first draw of the pair that places a walker for its `redraw`th try: the jitter in its cell at try 0, then the next
/// pairs after the orbit's draws.
constexpr u32 RedrawDrawX(u32 redraw) {
    return redraw == 0 ? DRAW_CENTRE_X : DRAW_REDRAW + (2 * (redraw - 1));
}

/// The `walker`th walker's orbit round `centre`: radius, wobble, starting angle, pace and direction drawn for it.
Orbit WalkerOrbit(u64 seed, u32 walker, core::Vec2 centre) {
    const f32 radius = ORBIT_RADIUS_MIN + (ORBIT_RADIUS_RANGE * WalkerUnit(seed, walker, DRAW_RADIUS));
    const f32 pace = PACE_MIN + (PACE_RANGE * WalkerUnit(seed, walker, DRAW_PACE));
    const bool clockwise = WalkerUnit(seed, walker, DRAW_DIRECTION) < WORLD_HALF;
    return {
        .centre = centre,
        .radius = radius,
        .wobble = radius * (WOBBLE_MIN + (WOBBLE_RANGE * WalkerUnit(seed, walker, DRAW_WOBBLE))),
        .phase = TURN * WalkerUnit(seed, walker, DRAW_PHASE),
        .speed = (clockwise ? pace : -pace) / radius,
    };
}

/// Where the `member`th walker of a grid of `columns` by `rows` cells over `area` circles: in its cell, row by row, at the
/// jitter `walker` draws.
core::Vec2 JitteredCellCentre(u64 seed, u32 walker, u32 member, u32 columns, u32 rows, const core::Rect& area, u32 redraw = 0) {
    const u32 drawX = RedrawDrawX(redraw);
    const u32 drawY = drawX + 1;
    const f32 width = (area.max.x - area.min.x) / static_cast<f32>(columns);
    const f32 height = (area.max.y - area.min.y) / static_cast<f32>(rows);
    const u32 column = member % columns;
    const u32 row = member / columns;
    return { .x = area.min.x + (width * (static_cast<f32>(column) + WalkerUnit(seed, walker, drawX))),
             .y = area.min.y + (height * (static_cast<f32>(row) + WalkerUnit(seed, walker, drawY))) };
}

void SpawnWalker(SceneContext& context, const Orbit& orbit) {
    const Look look{ .sprite = SpriteId::Villager, .shadow = true };
    const core::Entity entity = context.Spawn(OrbitPoint(orbit, 0), look);
    context.State()[ACTIVE].Entities().emplace<Orbit>(entity, orbit);
}

/// Whether `point` lies within `area`, its edges included.
bool Within(core::Vec2 point, const core::Rect& area) {
    return point.x >= area.min.x && point.x <= area.max.x && point.y >= area.min.y && point.y <= area.max.y;
}

// Of every ten of a city's people, this many live on the city's own tile; the rest are spread over its region.
constexpr u32 TENTHS_ON_CITY_TILE = 7;
constexpr u32 TENTH = 10;

// The stable ID of the place `index`'s name label: far above any creature's, which count up from 1.
constexpr u64 PLACE_ENTITY_BASE = 1ULL << 40;
// The gap between the city marker's lower edge and the name hanging from it, in world units.
constexpr f32 PLACE_NAME_GAP = 8.0F;

/// Spawns the cities' people, city by city in place order, `npc` counting them all: the first seven tenths of a city's share
/// on its own tile, each of the rest on a tile of its region drawn for its running index; each starts with StartingPower().
void SpawnPeople(SceneContext& context, const core::WorldMap& map) {
    const u64 seed = context.State().Seed();
    u32 npc = 0;
    for (std::size_t index = 0; index < map.Places().size(); ++index) {
        const core::Place& place = map.Places()[index];
        if (place.kind != core::PlaceKind::City) {
            continue;
        }
        const u16 city = static_cast<u16>(index);
        const u32 cityTile = map.TileIndex(place.tile);
        const std::span<const u32> region = map.RegionTiles(city);
        const u32 onCityTile = place.people * TENTHS_ON_CITY_TILE / TENTH;
        for (u32 member = 0; member < place.people; ++member, ++npc) {
            u32 tile = cityTile;
            if (member >= onCityTile && !region.empty()) {
                tile = region[core::ToBelow(core::Random(seed, 0, npc, PURPOSE_HOME_TILE, 0), static_cast<u32>(region.size()))];
            }
            // the NPC's identity is the next stable ID the state hands out
            const core::StableId identity{ context.State().Ids().next };
            const core::StableId spawned = SpawnNpc(context.State(), tile, city, StartingPower(seed, identity));
            PN_ASSERT(spawned == identity, "an NPC spawned under another stable ID than the one its power was drawn for");
        }
    }
}

} // namespace

core::Rect GatheringReach(const Gathering& gathering) {
    const core::Vec2 farthest = gathering.half + core::Vec2{ .x = ORBIT_REACH, .y = ORBIT_REACH };
    return { .min = gathering.point - farthest, .max = gathering.point + farthest };
}

std::vector<Gathering> WorldGatherings(const core::WorldMap& map) {
    std::vector<Gathering> gatherings;
    const u16 busiest = map.PlaceAt(map.TileAt(WORLD_BUSIEST));
    const auto onCity = [&map](std::size_t place, Gathering shape) {
        shape.point = map.TileCentre(map.TileIndex(map.Places()[place].tile));
        return shape;
    };
    const auto isCity = [&map](std::size_t place) { return map.Places()[place].kind == core::PlaceKind::City; };
    if (busiest != core::NO_PLACE && isCity(busiest)) {
        gatherings.push_back(onCity(busiest, BUSIEST));
    }
    u32 towns = 0;
    for (std::size_t place = 0; place < map.Places().size() && towns < TOWNS; ++place) {
        if (isCity(place) && place != map.Start() && place != busiest) {
            gatherings.push_back(onCity(place, TOWN));
            ++towns;
        }
    }
    return gatherings;
}

core::Vec2 SpreadDraw(u64 seed, u32 walker, u32 redraw) {
    const core::Vec2 margin{ .x = ORBIT_REACH, .y = ORBIT_REACH };
    const core::Rect area{ .min = WORLD_MAP.min + margin, .max = WORLD_MAP.max - margin };
    return JitteredCellCentre(seed, walker, walker, SPREAD_COLUMNS, SPREAD_ROWS, area, redraw);
}

std::optional<core::Vec2> SpreadCentre(const core::WorldMap& map, u64 seed, u32 walker) {
    for (u32 redraw = 0; redraw <= SPREAD_REDRAWS; ++redraw) {
        const core::Vec2 centre = SpreadDraw(seed, walker, redraw);
        if (map.CostAt(map.TileAt(centre)) != core::IMPASSABLE_COST) {
            return centre;
        }
    }
    return std::nullopt;
}

std::optional<core::Vec2> GatheredCentre(
    const core::WorldMap& map,
    u64 seed,
    u32 walker,
    u32 member,
    const Gathering& gathering
) {
    const core::Rect area{ .min = gathering.point - gathering.half, .max = gathering.point + gathering.half };
    const auto passable = [&map](core::Vec2 centre) { return map.CostAt(map.TileAt(centre)) != core::IMPASSABLE_COST; };
    const core::Vec2 cell = JitteredCellCentre(seed, walker, member, gathering.columns, gathering.rows, area);
    if (passable(cell)) {
        return cell;
    }
    // Anywhere in the area, not its own cell, which may be all sea: the crowd on land stays as many as it was.
    for (u32 redraw = 1; redraw <= GATHERED_REDRAWS; ++redraw) {
        const u32 drawX = RedrawDrawX(redraw);
        const core::Vec2 centre{
            .x = area.min.x + ((area.max.x - area.min.x) * WalkerUnit(seed, walker, drawX)),
            .y = area.min.y + ((area.max.y - area.min.y) * WalkerUnit(seed, walker, drawX + 1)),
        };
        if (passable(centre)) {
            return centre;
        }
    }
    return std::nullopt;
}

const SceneTraits& WorldScene::Traits() const {
    return WORLD_TRAITS;
}

std::optional<core::Vec2> WorldScene::Portal(const SceneContext& context) const {
    if (context.Data().world == nullptr) {
        return std::nullopt;
    }
    return context.Data().world->StartCentre() + WORLD_PORTAL_OFFSET;
}

void WorldScene::Build(SceneContext& context) const {
    const core::WorldMap& map = *context.Data().world;
    context.SpawnPlayer(map.StartCentre());
    const u64 seed = context.State().Seed();
    u32 walker = 0;
    for (; walker < SPREAD_WALKERS; ++walker) {
        if (const std::optional<core::Vec2> centre = SpreadCentre(map, seed, walker)) {
            SpawnWalker(context, WalkerOrbit(seed, walker, *centre));
        }
    }
    for (const Gathering& gathering : WorldGatherings(map)) {
        for (u32 member = 0; member < gathering.columns * gathering.rows; ++member, ++walker) {
            if (const std::optional<core::Vec2> centre = GatheredCentre(map, seed, walker, member, gathering)) {
                SpawnWalker(context, WalkerOrbit(seed, walker, *centre));
            }
        }
    }
    SpawnPeople(context, map);
    context.SeedTravel();
}

void WorldScene::Publish(const SceneContext& context, core::Rect view, core::Snapshot& snapshot) const {
    PN_PROFILE_ZONE();
    const core::WorldMap& map = *context.Data().world;
    // Every place's marker (world_look.yaml picks one per city and danger sort) shares the city marker's size below the
    // tile's centre; a test checks they do.
    const core::SpriteInfo& marker = context.Manifest().Sprite(core::atlas::SpriteId::MarkerCity);
    const f32 markerBelow = static_cast<f32>(marker.height - marker.footY);
    for (std::size_t index = 0; index < map.Places().size(); ++index) {
        const core::Place& place = map.Places()[index];
        const core::Vec2 centre = map.TileCentre(map.TileIndex(place.tile));
        if (!Within(centre, view)) {
            continue;
        }
        // The name hangs under the marker drawn round the tile's centre, like a map label under its town: the cultivator
        // stands on the start city with its own label over its head, so a name above the tile would sit on that label.
        const core::Vec2 anchor = centre + core::Vec2{ .x = 0.0F, .y = markerBelow + PLACE_NAME_GAP };
        snapshot.texts.push_back(
            {
                .kind = core::TextKind::Place,
                .string = place.name,
                .previous = anchor,
                .current = anchor,
                .entity = static_cast<core::StableId>(PLACE_ENTITY_BASE + index),
                .nameKey = std::to_underlying(place.name),
                .priority = core::LabelPriority::Place,
                .align = core::TextAlign::TopCentre,
            }
        );
    }
    const core::Registry& entities = context.State()[ACTIVE].Entities();
    for (const core::Entity entity : entities.view<const Orbit>()) {
        const core::StableId stableId = entities.get<core::StableId>(entity);
        const Body& body = entities.get<Body>(entity);
        if (std::to_underlying(stableId) % LABELLED_EVERY != 0 || !Within(body.current, view)) {
            continue;
        }
        const Look& look = entities.get<Look>(entity);
        const f32 height = static_cast<f32>(context.Manifest().Sprite(look.sprite).footY) * look.scale;
        const core::Vec2 head{ .x = 0.0F, .y = -height };
        snapshot.texts.push_back(
            {
                .kind = core::TextKind::Label,
                .string = core::StringId::WalkerName,
                .previous = body.previous + head,
                .current = body.current + head,
                .offset = LABEL_OFFSET,
                .entity = stableId,
                .height = height,
                .nameKey = std::to_underlying(core::StringId::WalkerName),
            }
        );
    }
}

} // namespace game
