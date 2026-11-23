#include "core/journey.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/random.hpp"

namespace core {

namespace {

/// A day in a journey's signed ticks.
constexpr i64 DAY_TICKS = static_cast<i64>(TICKS_PER_DAY);
/// The purpose of a stay's day points, apart from every other use of core::Random.
constexpr u32 STAY_PURPOSE = 0x5354'4159; // "STAY"
constexpr u32 STAY_DRAW_X = 0;
constexpr u32 STAY_DRAW_Y = 1;
/// A day point lies in a square of this side, in tiles, centred on the city's tile centre: its corners are 0.34 of a tile
/// out, within the 0.35 bound on how far a day point may stray, and so is every point between two of them.
constexpr f64 STAY_SIDE = 0.48;
constexpr f64 UNIT_MIDDLE = 0.5;
/// Members after the first stand on a hexagonal lattice this far apart, in world units.
constexpr f32 MEMBER_SPACING = 24.0F;
constexpr std::size_t HEX_SIDES = 6;
constexpr f32 HEX_HALF = 0.5F;
constexpr f32 HEX_HEIGHT = std::numbers::sqrt3_v<f32> * HEX_HALF;
/// The six unit steps of the lattice, a sixth of a turn apart.
constexpr std::array<Vec2, HEX_SIDES> HEX_STEPS{ { { .x = 1.0F, .y = 0.0F },
                                                   { .x = HEX_HALF, .y = HEX_HEIGHT },
                                                   { .x = -HEX_HALF, .y = HEX_HEIGHT },
                                                   { .x = -1.0F, .y = 0.0F },
                                                   { .x = -HEX_HALF, .y = -HEX_HEIGHT },
                                                   { .x = HEX_HALF, .y = -HEX_HEIGHT } } };

/// The point `fraction` of the way from `from` to `target`.
Vec2 Between(Vec2 from, Vec2 target, f64 fraction) {
    return from + ((target - from) * static_cast<f32>(fraction));
}

/// The point `leg` holds at its tile `index`: its origin for the first, the tile's centre after.
Vec2 LegPoint(const WorldMap& map, const Leg& leg, std::size_t index) {
    return index == 0 ? leg.origin : map.TileCentre(leg.tiles[index]);
}

/// Where the walker of `stay` aims at its day point `day`: on the city's tile centre at its first (its start) and at any
/// day point at or past its end, else a point drawn round it.
Vec2 StayPoint(const WorldMap& map, const Stay& stay, Vec2 centre, i64 day) {
    if (day <= 0 || stay.startTick + (day * DAY_TICKS) >= stay.endTick) {
        return centre;
    }
    const auto offset = [&](u32 draw) {
        const f64 unit = ToUnit(Random(stay.seed, 0, static_cast<u64>(day), STAY_PURPOSE, draw));
        return static_cast<f32>((unit - UNIT_MIDDLE) * STAY_SIDE * static_cast<f64>(map.TileSize()));
    };
    return centre + Vec2{ .x = offset(STAY_DRAW_X), .y = offset(STAY_DRAW_Y) };
}

/// Where the walker of `stay` is at `tick`: from one day point to the next across each day, the last day running to the
/// stay's end, which may come before the day is whole.
Vec2 StayWhere(const WorldMap& map, const Stay& stay, i64 tick) {
    PN_ASSERT(stay.city < map.Places().size(), "a stay is in a place of the map");
    const auto cityCentre = [&] { return map.TileCentre(map.TileIndex(map.Places()[stay.city].tile)); };
    if (tick <= stay.startTick || tick >= stay.endTick) {
        return cityCentre();
    }
    const Vec2 centre = cityCentre();
    const i64 day = (tick - stay.startTick) / DAY_TICKS;
    const i64 dayStart = stay.startTick + (day * DAY_TICKS);
    const i64 dayEnd = std::min(dayStart + DAY_TICKS, stay.endTick);
    const f64 fraction = static_cast<f64>(tick - dayStart) / static_cast<f64>(dayEnd - dayStart);
    return Between(StayPoint(map, stay, centre, day), StayPoint(map, stay, centre, day + 1), fraction);
}

/// Where member `member` stands from the journey's position: member 0 on it, the others filling the hexagonal lattice's
/// rings outwards, ring k holding 6k places, each ring walked from its corner on the right round its six sides.
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

/// A journey walker's walk at one tick, gathered from the straight stretches it walks, given in time order.
class WalkTally {
public:
    /// Gathers the walk at `tick`.
    explicit WalkTally(i64 tick)
      : m_tick{ tick } {
    }

    /// Adds the stretch from `from` to `target`, walked evenly over the ticks `start` to `end`. True once no later stretch
    /// can change the walk: after a stretch not yet begun none is, so only the facing may still be unknown.
    bool Add(Vec2 from, Vec2 target, i64 start, i64 end) {
        const f64 across = static_cast<f64>(target.x) - static_cast<f64>(from.x);
        if (start >= m_tick) {
            if (!m_faced && across != 0.0) {
                Face(across);
            }
            return m_faced;
        }
        const f64 down = static_cast<f64>(target.y) - static_cast<f64>(from.y);
        const f64 length = std::sqrt((across * across) + (down * down));
        // Begun, so `end` is past `start` whenever the tick lies before it.
        const f64 share = m_tick >= end ? 1.0 : static_cast<f64>(m_tick - start) / static_cast<f64>(end - start);
        m_walked.distance += length * share;
        m_walked.moving = m_walked.moving || (length > 0.0 && m_tick <= end);
        if (across != 0.0) {
            Face(across);
        }
        return false;
    }

    /// The walk from the stretches added so far.
    [[nodiscard]] Walked Result() const {
        return m_walked;
    }

private:
    /// Faces the way a stretch `across` units to the right (left when negative) goes.
    void Face(f64 across) {
        m_walked.facesLeft = across < 0.0;
        m_faced = true;
    }

    i64 m_tick;
    Walked m_walked{};
    /// Whether a stretch across has set the facing.
    bool m_faced = false;
};

/// Adds `leg`'s steps, from tile to tile, to `tally`; true once it needs no more.
bool AddLeg(const WorldMap& map, const Leg& leg, WalkTally& tally) {
    PN_ASSERT(leg.arrive.size() == leg.tiles.size(), "a leg has an arrive tick per tile");
    for (std::size_t index = 0; index + 1 < leg.tiles.size(); ++index) {
        const i64 start = leg.startTick + i64{ leg.arrive[index] };
        const i64 end = leg.startTick + i64{ leg.arrive[index + 1] };
        if (tally.Add(LegPoint(map, leg, index), LegPoint(map, leg, index + 1), start, end)) {
            return true;
        }
    }
    return false;
}

/// Adds `stay`'s days, from day point to day point as StayWhere() moves its walker, to `tally`; true once it needs no
/// more.
bool AddStay(const WorldMap& map, const Stay& stay, WalkTally& tally) {
    PN_ASSERT(stay.city < map.Places().size(), "a stay is in a place of the map");
    const Vec2 centre = map.TileCentre(map.TileIndex(map.Places()[stay.city].tile));
    Vec2 from = centre;
    for (i64 day = 0; stay.startTick + (day * DAY_TICKS) < stay.endTick; ++day) {
        const i64 dayStart = stay.startTick + (day * DAY_TICKS);
        const Vec2 target = StayPoint(map, stay, centre, day + 1);
        if (tally.Add(from, target, dayStart, std::min(dayStart + DAY_TICKS, stay.endTick))) {
            return true;
        }
        from = target;
    }
    return false;
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

std::optional<Leg> MakePacedLeg(const WorldMap& map, i64 startTick, Vec2 origin, std::span<const u32> tiles, f32 unitsPerTick) {
    PN_ASSERT(!tiles.empty(), "a leg has a tile to start on");
    if (!(unitsPerTick > 0.0F)) {
        return std::nullopt;
    }
    Leg leg{ .shape = LegShape::TileRoute,
             .profile = WALKING,
             .startTick = startTick,
             .origin = origin,
             .tiles = { tiles.begin(), tiles.end() },
             .arrive = {} };
    leg.arrive.reserve(tiles.size());
    leg.arrive.push_back(0);
    f64 walked = 0.0;
    Vec2 from = origin;
    for (std::size_t index = 1; index < tiles.size(); ++index) {
        PN_ASSERT(
            map.StepCost(tiles[index - 1], tiles[index], WALKING) != IMPASSABLE_COST, "a leg steps between adjacent tiles"
        );
        const Vec2 centre = map.TileCentre(tiles[index]);
        const Vec2 apart = centre - from;
        walked += std::sqrt(static_cast<f64>((apart.x * apart.x) + (apart.y * apart.y)));
        from = centre;
        constexpr u64 MOST_TICKS = std::numeric_limits<u32>::max();
        // Checked in floating point, before any conversion: a float out of an integer's range converts to anything.
        const f64 ticks = std::ceil(walked / static_cast<f64>(unitsPerTick));
        if (!(ticks <= static_cast<f64>(MOST_TICKS))) {
            return std::nullopt;
        }
        const u64 arrive = std::max(static_cast<u64>(ticks), u64{ leg.arrive.back() } + 1);
        if (arrive > MOST_TICKS) {
            return std::nullopt;
        }
        leg.arrive.push_back(static_cast<u32>(arrive));
    }
    return leg;
}

i64 EndTick(const Leg& leg) {
    return leg.arrive.empty() ? leg.startTick : leg.startTick + i64{ leg.arrive.back() };
}

i64 StartTick(const JourneyPart& part) {
    return part.stay ? part.rest.startTick : part.leg.startTick;
}

i64 EndTick(const JourneyPart& part) {
    return part.stay ? part.rest.endTick : EndTick(part.leg);
}

i64 EndTick(const Journey& journey) {
    return journey.parts.empty() ? 0 : EndTick(journey.parts.back());
}

i64 StartTick(const Journey& journey) {
    return journey.parts.empty() ? 0 : StartTick(journey.parts.front());
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
    PN_ASSERT(member < journey.members.size(), "a journey places only its own members");
    if (journey.parts.empty()) {
        return {};
    }
    // The first part ending after `tick` holds it (the next part on a boundary, where both give the same point); past the
    // end, the last part holds its end.
    const auto holding = std::ranges::find_if(journey.parts, [tick](const JourneyPart& part) { return EndTick(part) > tick; });
    const JourneyPart& part = holding == journey.parts.end() ? journey.parts.back() : *holding;
    const Vec2 position = part.stay ? StayWhere(map, part.rest, tick) : Where(map, part.leg, tick);
    return position + MemberOffset(member);
}

Walked WalkedAt(const WorldMap& map, const Journey& journey, i64 tick) {
    WalkTally tally{ tick };
    for (const JourneyPart& part : journey.parts) {
        if (part.stay ? AddStay(map, part.rest, tally) : AddLeg(map, part.leg, tally)) {
            break;
        }
    }
    return tally.Result();
}

bool Underway(const Journey& journey, i64 tick) {
    return !journey.parts.empty() && tick >= StartTick(journey) && tick < EndTick(journey);
}

Journey Visit(
    const WorldMap& map,
    std::span<const u32> out,
    ProfileIndex profile,
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
    Leg outward = MakeTileLeg(map, profile, departTick, map.TileCentre(out.front()), out);
    const Stay stay{ .city = destination,
                     .startTick = EndTick(outward),
                     .endTick = EndTick(outward) + (i64{ stayDays } * DAY_TICKS),
                     .seed = seed };
    const std::vector<u32> back{ out.rbegin(), out.rend() };
    Leg homeward = MakeTileLeg(map, profile, stay.endTick, map.TileCentre(out.back()), back);
    journey.parts.reserve(3);
    journey.parts.push_back({ .stay = false, .leg = std::move(outward), .rest = {} });
    journey.parts.push_back({ .stay = true, .leg = {}, .rest = stay });
    journey.parts.push_back({ .stay = false, .leg = std::move(homeward), .rest = {} });
    return journey;
}

std::optional<VisitPlan> PlanOf(const Journey& journey) {
    if (journey.members.empty() || journey.parts.size() != 3) {
        return std::nullopt;
    }
    const JourneyPart& first = journey.parts[0];
    const JourneyPart& middle = journey.parts[1];
    const JourneyPart& last = journey.parts[2];
    const Leg& out = first.leg;
    const Leg& back = last.leg;
    const Stay& stay = middle.rest;
    const bool shaped = !first.stay && middle.stay && !last.stay && out.shape == LegShape::TileRoute &&
                        back.shape == LegShape::TileRoute && out.profile == back.profile && !out.tiles.empty() &&
                        std::ranges::equal(back.tiles.begin(), back.tiles.end(), out.tiles.rbegin(), out.tiles.rend());
    if (!shaped || stay.city != journey.destination || stay.startTick != EndTick(out) || back.startTick != stay.endTick ||
        stay.endTick < stay.startTick) {
        return std::nullopt;
    }
    // Ordered, the difference of two i64s fits a u64.
    const u64 span = static_cast<u64>(stay.endTick) - static_cast<u64>(stay.startTick);
    const u64 days = span / static_cast<u64>(DAY_TICKS);
    if (span % static_cast<u64>(DAY_TICKS) != 0 || days > std::numeric_limits<u32>::max()) {
        return std::nullopt;
    }
    return VisitPlan{ .members = journey.members,
                      .home = journey.home,
                      .destination = journey.destination,
                      .from = out.tiles.front(),
                      .departTick = out.startTick,
                      .stayDays = static_cast<u32>(days),
                      .seed = stay.seed,
                      .profile = out.profile };
}

Journey Rebuild(const WorldMap& map, Paths& paths, const VisitPlan& plan) {
    PN_ASSERT(!plan.members.empty(), "a plan has a member");
    PN_ASSERT(plan.from < map.Columns() * map.Rows(), "a plan starts on a tile of the map");
    PN_ASSERT(plan.profile < map.Profiles().size(), "a plan's profile is one of the map's");
    Journey journey{ .members = plan.members, .home = plan.home, .destination = plan.destination, .parts = {} };
    // Visit's way home ends by stepping onto this tile, which the profile can't enter.
    if (map.CostAt(plan.from, plan.profile) == IMPASSABLE_COST) {
        return journey;
    }
    const std::vector<u32> route = RouteDown(map, paths.To(plan.destination, plan.profile), plan.from);
    if (route.empty()) {
        return journey;
    }
    Journey visit = Visit(
        map, route, plan.profile, plan.members.front(), plan.home, plan.destination, plan.departTick, plan.stayDays, plan.seed
    );
    journey.parts = std::move(visit.parts);
    return journey;
}

} // namespace core
