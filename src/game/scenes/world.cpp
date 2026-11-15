#include "game/scenes/world.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"
#include "core/random.hpp"
#include "core/view_limits.hpp"
#include "game/npcs.hpp"
#include "game/random_purposes.hpp"

namespace game {

namespace {

using core::atlas::SpriteId;

constexpr f32 WORLD_HALF = 0.5F;
constexpr core::Vec2 WORLD_CENTRE = (WORLD_MAP.min + WORLD_MAP.max) * WORLD_HALF;

// The portal, a short walk from the start up and to the left: on the first screen, so it is found at once, in the quiet
// country round the start, which no gathering reaches (ClearOfStart()), and west of it, off the strip east of the start
// that `--camera-path` walks the cultivator along, so a measured run never changes scene (app's camera_path_tests holds
// this).
constexpr core::Vec2 WORLD_PORTAL = WORLD_CENTRE + core::Vec2{ .x = -560.0F, .y = -200.0F };

constexpr SceneTraits WORLD_TRAITS{
    .name = "world",
    .view = WORLD_MAP,
    .home = WORLD_CENTRE,
    .creatures = true,
    .camera = core::CameraKind::Roam,
    .worldMap = true,
    .portal = WORLD_PORTAL,
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

// The walkers' orbits.
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
struct Gathering {
    core::Vec2 point{}; // the area's middle
    core::Vec2 half{};  // half its width and height
    u32 columns = 0;    // cells across
    u32 rows = 0;       // cells down; one walker each
};

// The busiest gathering fills a 7,680 by 4,320 view round WORLD_BUSIEST and a margin for the orbits' reach:
// 2,900 walkers on 8,880 by 5,520 units, about 123 a screen (1,920 by 1,080); the orbits' scatter makes a screen's count
// vary by about 20 either way, so any denser would pile up past 150 and any thinner leave the view short of 2,000. These
// counts depend on the walker draws, their purposes and the orbit ranges: change one and retune the density, never the tests.
// The towns are 4,800 by 2,700: 375 walkers each, about 60 a screen. All, their walkers' orbits included, stand clear of
// the widest view round the start, which is 15,360 by 8,640, and of each other (both checked below).
constexpr std::array<Gathering, 4> GATHERINGS{ {
    { .point = WORLD_BUSIEST, .half = { .x = 4'440.0F, .y = 2'760.0F }, .columns = 58, .rows = 50 },
    { .point = { .x = 8'000.0F, .y = 5'600.0F }, .half = { .x = 2'400.0F, .y = 1'350.0F }, .columns = 25, .rows = 15 },
    { .point = { .x = 30'500.0F, .y = 4'500.0F }, .half = { .x = 2'400.0F, .y = 1'350.0F }, .columns = 25, .rows = 15 },
    { .point = { .x = 9'600.0F, .y = 17'800.0F }, .half = { .x = 2'400.0F, .y = 1'350.0F }, .columns = 25, .rows = 15 },
} };
// The spread crowd: 50 by 32 cells over the map less its margins, about 4 walkers a screen.
constexpr u32 SPREAD_COLUMNS = 50;
constexpr u32 SPREAD_ROWS = 32;
constexpr u32 SPREAD_WALKERS = SPREAD_COLUMNS * SPREAD_ROWS;

// The area `gathering`'s walkers' feet can reach: its own, grown by the orbits' reach.
constexpr core::Rect Reached(const Gathering& gathering) {
    const core::Vec2 farthest = gathering.half + core::Vec2{ .x = ORBIT_REACH, .y = ORBIT_REACH };
    return { .min = gathering.point - farthest, .max = gathering.point + farthest };
}

// Whether every walker of `gathering` stays on the map, out to its area's edge and its orbit's.
constexpr bool OnTheMap(const Gathering& gathering) {
    const core::Rect reached = Reached(gathering);
    return reached.min.x >= WORLD_MAP.min.x && reached.max.x <= WORLD_MAP.max.x && reached.min.y >= WORLD_MAP.min.y &&
           reached.max.y <= WORLD_MAP.max.y;
}
static_assert(std::ranges::all_of(GATHERINGS, OnTheMap), "a gathering whose walkers would stand off the map");

// Whether no walker of `gathering` can stand in the widest view round the start.
constexpr bool ClearOfStart(const Gathering& gathering) {
    const core::Rect reached = Reached(gathering);
    const core::Rect widest{ .min = WORLD_CENTRE - core::WIDEST_HALF_VIEW, .max = WORLD_CENTRE + core::WIDEST_HALF_VIEW };
    return reached.max.x < widest.min.x || reached.min.x > widest.max.x || reached.max.y < widest.min.y ||
           reached.min.y > widest.max.y;
}
static_assert(std::ranges::all_of(GATHERINGS, ClearOfStart), "a gathering whose walkers could stand in view of the start");

// Whether no walker of one gathering can stand where a walker of another could: two overlapping would pile up.
constexpr bool ApartFromEachOther() {
    for (std::size_t first = 0; first < GATHERINGS.size(); ++first) {
        for (std::size_t second = first + 1; second < GATHERINGS.size(); ++second) {
            const core::Rect one = Reached(GATHERINGS.at(first));
            const core::Rect other = Reached(GATHERINGS.at(second));
            const bool apart =
                one.max.x < other.min.x || one.min.x > other.max.x || one.max.y < other.min.y || one.min.y > other.max.y;
            if (!apart) {
                return false;
            }
        }
    }
    return true;
}
static_assert(ApartFromEachOther(), "two gatherings whose walkers could stand in the same place");

// The numbers each walker draws (core::Random's draw index).
constexpr u32 DRAW_CENTRE_X = 0; // the jitter within its cell
constexpr u32 DRAW_CENTRE_Y = 1;
constexpr u32 DRAW_RADIUS = 2;
constexpr u32 DRAW_WOBBLE = 3;
constexpr u32 DRAW_PHASE = 4;
constexpr u32 DRAW_PACE = 5;
constexpr u32 DRAW_DIRECTION = 6;

// A walker's random number `draw`, as a value in [0, 1).
f32 WalkerUnit(u64 seed, u32 walker, u32 draw) {
    return static_cast<f32>(core::ToUnit(core::Random(seed, 0, walker, PURPOSE_WALKER, draw)));
}

// The `walker`th walker's orbit round `centre`: radius, wobble, starting angle, pace and direction drawn for it.
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

// Where the `member`th walker of a grid of `columns` by `rows` cells over `area` circles: in its cell, row by row, at the
// jitter `walker` draws.
core::Vec2 JitteredCellCentre(u64 seed, u32 walker, u32 member, u32 columns, u32 rows, const core::Rect& area) {
    const f32 width = (area.max.x - area.min.x) / static_cast<f32>(columns);
    const f32 height = (area.max.y - area.min.y) / static_cast<f32>(rows);
    const u32 column = member % columns;
    const u32 row = member / columns;
    return { .x = area.min.x + (width * (static_cast<f32>(column) + WalkerUnit(seed, walker, DRAW_CENTRE_X))),
             .y = area.min.y + (height * (static_cast<f32>(row) + WalkerUnit(seed, walker, DRAW_CENTRE_Y))) };
}

// Where the `walker`th walker of the spread crowd circles: anywhere on the map, away from its edges.
core::Vec2 SpreadCentre(u64 seed, u32 walker) {
    const core::Vec2 margin{ .x = ORBIT_REACH, .y = ORBIT_REACH };
    return JitteredCellCentre(
        seed, walker, walker, SPREAD_COLUMNS, SPREAD_ROWS, { .min = WORLD_MAP.min + margin, .max = WORLD_MAP.max - margin }
    );
}

// Where the `walker`th walker, the `member`th of `gathering`, circles: in its area.
core::Vec2 GatheredCentre(u64 seed, u32 walker, u32 member, const Gathering& gathering) {
    return JitteredCellCentre(
        seed,
        walker,
        member,
        gathering.columns,
        gathering.rows,
        { .min = gathering.point - gathering.half, .max = gathering.point + gathering.half }
    );
}

void SpawnWalker(SceneContext& context, const Orbit& orbit) {
    const Look look{ .sprite = SpriteId::Villager, .shadow = true };
    const core::Entity entity = context.Spawn(OrbitPoint(orbit, 0), look);
    context.State()[ACTIVE].Entities().emplace<Orbit>(entity, orbit);
}

// Whether `point` lies within `area`, its edges included.
bool Within(core::Vec2 point, const core::Rect& area) {
    return point.x >= area.min.x && point.x <= area.max.x && point.y >= area.min.y && point.y <= area.max.y;
}

} // namespace

const SceneTraits& WorldScene::Traits() const {
    return WORLD_TRAITS;
}

void WorldScene::Build(SceneContext& context) const {
    context.SpawnPlayer(WORLD_CENTRE);
    const u64 seed = context.State().Seed();
    u32 walker = 0;
    for (; walker < SPREAD_WALKERS; ++walker) {
        SpawnWalker(context, WalkerOrbit(seed, walker, SpreadCentre(seed, walker)));
    }
    for (const Gathering& gathering : GATHERINGS) {
        for (u32 member = 0; member < gathering.columns * gathering.rows; ++member, ++walker) {
            SpawnWalker(context, WalkerOrbit(seed, walker, GatheredCentre(seed, walker, member, gathering)));
        }
    }
}

void WorldScene::Publish(const SceneContext& context, core::Rect view, core::Snapshot& snapshot) const {
    PN_PROFILE_ZONE();
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
