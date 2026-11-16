#include "core/journey.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/random.hpp"

namespace core {

namespace {

// The purpose of a stay's day points, apart from every other use of core::Random.
constexpr u32 STAY_PURPOSE = 0x5354'4159; // "STAY"
constexpr u32 STAY_DRAW_X = 0;
constexpr u32 STAY_DRAW_Y = 1;
// A day point lies in a square of this side, in tiles, centred on the city's tile centre: its corners are 0.34 of a tile
// out, within the 0.35 the rulings allow, and so is every point between two of them.
constexpr f64 STAY_SIDE = 0.48;
constexpr f64 UNIT_MIDDLE = 0.5;
// Members after the first stand on a hexagonal lattice this far apart, in world units.
constexpr f32 MEMBER_SPACING = 24.0F;
constexpr std::size_t HEX_SIDES = 6;
constexpr f32 HEX_HALF = 0.5F;
constexpr f32 HEX_HEIGHT = std::numbers::sqrt3_v<f32> * HEX_HALF;
// The six unit steps of the lattice, a sixth of a turn apart.
constexpr std::array<Vec2, HEX_SIDES> HEX_STEPS{ { { .x = 1.0F, .y = 0.0F },
                                                   { .x = HEX_HALF, .y = HEX_HEIGHT },
                                                   { .x = -HEX_HALF, .y = HEX_HEIGHT },
                                                   { .x = -1.0F, .y = 0.0F },
                                                   { .x = -HEX_HALF, .y = -HEX_HEIGHT },
                                                   { .x = HEX_HALF, .y = -HEX_HEIGHT } } };

// The point `fraction` of the way from `from` to `target`.
Vec2 Between(Vec2 from, Vec2 target, f64 fraction) {
    return from + ((target - from) * static_cast<f32>(fraction));
}

// The point `leg` holds at its tile `index`: its origin for the first, the tile's centre after.
Vec2 LegPoint(const WorldMap& map, const Leg& leg, std::size_t index) {
    return index == 0 ? leg.origin : map.TileCentre(leg.tiles[index]);
}

// The ticks `part` starts and ends on.
i64 PartStart(const JourneyPart& part) {
    return part.stay ? part.rest.startTick : part.leg.startTick;
}

i64 PartEnd(const JourneyPart& part) {
    return part.stay ? part.rest.endTick : EndTick(part.leg);
}

// Where the walker of `stay` aims at its day point `day`: on the city's tile centre at its first (its start) and at any
// day point at or past its end, else a point drawn round it.
Vec2 StayPoint(const WorldMap& map, const Stay& stay, Vec2 centre, i64 day) {
    if (day <= 0 || stay.startTick + (day * STAY_DAY_TICKS) >= stay.endTick) {
        return centre;
    }
    const auto offset = [&](u32 draw) {
        const f64 unit = ToUnit(Random(stay.seed, 0, static_cast<u64>(day), STAY_PURPOSE, draw));
        return static_cast<f32>((unit - UNIT_MIDDLE) * STAY_SIDE * static_cast<f64>(map.TileSize()));
    };
    return centre + Vec2{ .x = offset(STAY_DRAW_X), .y = offset(STAY_DRAW_Y) };
}

// Where the walker of `stay` is at `tick`: from one day point to the next across each day, the last day running to the
// stay's end, which may come before the day is whole.
Vec2 StayWhere(const WorldMap& map, const Stay& stay, i64 tick) {
    PN_ASSERT(stay.city < map.Places().size(), "a stay is in a place of the map");
    const auto cityCentre = [&] { return map.TileCentre(map.TileIndex(map.Places()[stay.city].tile)); };
    if (tick <= stay.startTick || tick >= stay.endTick) {
        return cityCentre();
    }
    const Vec2 centre = cityCentre();
    const i64 day = (tick - stay.startTick) / STAY_DAY_TICKS;
    const i64 dayStart = stay.startTick + (day * STAY_DAY_TICKS);
    const i64 dayEnd = std::min(dayStart + STAY_DAY_TICKS, stay.endTick);
    const f64 fraction = static_cast<f64>(tick - dayStart) / static_cast<f64>(dayEnd - dayStart);
    return Between(StayPoint(map, stay, centre, day), StayPoint(map, stay, centre, day + 1), fraction);
}

// Where member `member` stands from the journey's position: member 0 on it, the others filling the hexagonal lattice's
// rings outwards, ring k holding 6k places, each ring walked from its corner on the right round its six sides.
Vec2 MemberOffset(std::size_t member) {
    if (member == 0) {
        return {};
    }
    std::size_t ring = 1;
    std::size_t place = member - 1;
    while (place >= HEX_SIDES * ring) {
        place -= HEX_SIDES * ring;
        ++ring;
    }
    const std::size_t side = place / ring;
    const std::size_t along = place % ring;
    const Vec2 corner = HEX_STEPS.at(side) * static_cast<f32>(ring);
    const Vec2 step = HEX_STEPS.at((side + 2) % HEX_SIDES) * static_cast<f32>(along);
    return (corner + step) * MEMBER_SPACING;
}

} // namespace

Leg MakeTileLeg(const WorldMap& map, ProfileIndex profile, i64 startTick, Vec2 origin, std::span<const u32> tiles) {
    PN_ASSERT(!tiles.empty(), "a leg has a tile to start on");
    PN_ASSERT(profile < map.Profiles().size(), "a leg's profile is one of the map's");
    const u64 ticksPerCost = map.Profiles()[profile].ticksPerCost;
    Leg leg{ .shape = LegShape::TileRoute,
             .profile = profile,
             .startTick = startTick,
             .origin = origin,
             .tiles = { tiles.begin(), tiles.end() },
             .arrive = {} };
    leg.arrive.reserve(tiles.size());
    u64 ticks = 0;
    leg.arrive.push_back(0);
    for (std::size_t index = 1; index < tiles.size(); ++index) {
        const u32 cost = map.StepCost(tiles[index - 1], tiles[index], profile);
        PN_ASSERT(cost != IMPASSABLE_COST, "a leg steps between adjacent tiles its profile can walk");
        ticks += u64{ cost } * ticksPerCost;
        PN_ASSERT(ticks <= std::numeric_limits<u32>::max(), "a leg's ticks fit its arrive ticks");
        leg.arrive.push_back(static_cast<u32>(ticks));
    }
    return leg;
}

i64 EndTick(const Leg& leg) {
    return leg.arrive.empty() ? leg.startTick : leg.startTick + i64{ leg.arrive.back() };
}

i64 EndTick(const Journey& journey) {
    return journey.parts.empty() ? 0 : PartEnd(journey.parts.back());
}

i64 StartTick(const Journey& journey) {
    return journey.parts.empty() ? 0 : PartStart(journey.parts.front());
}

Vec2 Where(const WorldMap& map, const Leg& leg, i64 tick) {
    PN_ASSERT(!leg.tiles.empty() && leg.arrive.size() == leg.tiles.size(), "a leg has an arrive tick per tile");
    if (tick <= leg.startTick) {
        return leg.origin;
    }
    const std::size_t last = leg.tiles.size() - 1;
    if (tick >= EndTick(leg)) {
        return LegPoint(map, leg, last);
    }
    // Before its end, the elapsed ticks lie below the last arrive tick, so they fit a u32 and some tile comes after them.
    const auto elapsed = static_cast<u32>(tick - leg.startTick);
    const auto next = static_cast<std::size_t>(std::ranges::upper_bound(leg.arrive, elapsed) - leg.arrive.begin());
    const std::size_t from = next - 1;
    const f64 fraction = static_cast<f64>(elapsed - leg.arrive[from]) / static_cast<f64>(leg.arrive[next] - leg.arrive[from]);
    return Between(LegPoint(map, leg, from), LegPoint(map, leg, next), fraction);
}

Vec2 Where(const WorldMap& map, const Journey& journey, i64 tick, std::size_t member) {
    if (journey.parts.empty()) {
        return {};
    }
    // The first part ending after `tick` holds it (the next part on a boundary, where both give the same point); past the
    // end, the last part holds its end.
    const auto holding = std::ranges::find_if(journey.parts, [tick](const JourneyPart& part) { return PartEnd(part) > tick; });
    const JourneyPart& part = holding == journey.parts.end() ? journey.parts.back() : *holding;
    const Vec2 position = part.stay ? StayWhere(map, part.rest, tick) : Where(map, part.leg, tick);
    return position + MemberOffset(member);
}

bool Underway(const Journey& journey, i64 tick) {
    return !journey.parts.empty() && tick >= StartTick(journey) && tick < EndTick(journey);
}

Journey Visit(
    const WorldMap& map,
    std::span<const u32> out,
    StableId member,
    u16 home,
    u16 destination,
    i64 departTick,
    u32 stayDays,
    u32 seed
) {
    PN_ASSERT(!out.empty(), "a visit has a route out");
    PN_ASSERT(
        destination < map.Places().size() && map.TileIndex(map.Places()[destination].tile) == out.back(),
        "a visit's route ends on its destination's tile, where its stay starts"
    );
    Journey journey{ .members = { member }, .home = home, .destination = destination, .parts = {} };
    Leg outward = MakeTileLeg(map, WALKING, departTick, map.TileCentre(out.front()), out);
    const Stay stay{ .city = destination,
                     .startTick = EndTick(outward),
                     .endTick = EndTick(outward) + (i64{ stayDays } * STAY_DAY_TICKS),
                     .seed = seed };
    const std::vector<u32> back{ out.rbegin(), out.rend() };
    Leg homeward = MakeTileLeg(map, WALKING, stay.endTick, map.TileCentre(out.back()), back);
    journey.parts.reserve(3);
    journey.parts.push_back({ .stay = false, .leg = std::move(outward), .rest = {} });
    journey.parts.push_back({ .stay = true, .leg = {}, .rest = stay });
    journey.parts.push_back({ .stay = false, .leg = std::move(homeward), .rest = {} });
    return journey;
}

} // namespace core
