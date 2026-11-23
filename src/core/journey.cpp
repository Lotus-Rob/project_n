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

/// A straight stretch's length and the way it goes across.
struct Stretch {
    f64 length = 0.0;
    Across across = Across::None;
};

/// The stretch from `from` to `target`.
Stretch StretchOf(Vec2 from, Vec2 target) {
    const f64 across = static_cast<f64>(target.x) - static_cast<f64>(from.x);
    const f64 down = static_cast<f64>(target.y) - static_cast<f64>(from.y);
    Stretch stretch{ .length = std::sqrt((across * across) + (down * down)), .across = Across::None };
    if (across < 0.0) {
        stretch.across = Across::Left;
    }
    else if (across > 0.0) {
        stretch.across = Across::Right;
    }
    return stretch;
}

/// The share of a stretch walked evenly over the ticks `start` to `end` that is gone at `tick`, a tick after `start`.
f64 ShareGone(i64 start, i64 end, i64 tick) {
    // Begun, so `end` is past `start` whenever the tick lies before it.
    return tick >= end ? 1.0 : static_cast<f64>(tick - start) / static_cast<f64>(end - start);
}

/// One part's walk at a tick, over its own stretches begun by then.
struct PartWalked {
    /// The world units walked from the part's start.
    f64 distance = 0.0;
    /// Whether a stretch of nonzero length holds the tick.
    bool moving = false;
    /// The way its last stretch across begun by the tick goes.
    Across faced = Across::None;
    /// Its first stretch not yet begun by the tick, if any: a tile index of a leg, a day of a stay.
    std::optional<std::size_t> ahead = std::nullopt;
};

/// How many of `leg`'s stretches have begun by `tick`: those from the tiles it reaches before the tick.
std::size_t BegunStretches(const Leg& leg, i64 tick) {
    PN_ASSERT(!leg.tiles.empty() && leg.arrive.size() == leg.tiles.size(), "a leg has an arrive tick per tile");
    const i64 elapsed = tick - leg.startTick;
    // The last tile starts no stretch.
    const std::span<const u32> starts = std::span{ leg.arrive }.first(leg.arrive.size() - 1);
    const auto first = std::ranges::lower_bound(starts, elapsed, std::ranges::less{}, [](u32 arrive) { return i64{ arrive }; });
    return static_cast<std::size_t>(first - starts.begin());
}

/// `leg`'s walk at `tick`, `begun` of its stretches begun (BegunStretches()), read from its walk table `walk` (null only
/// when none has begun).
PartWalked LegWalked(const Leg& leg, i64 tick, std::size_t begun, const LegWalk* walk) {
    PartWalked walked;
    if (begun + 1 < leg.tiles.size()) {
        walked.ahead = begun;
    }
    if (begun == 0) {
        return walked;
    }
    PN_ASSERT(walk != nullptr && walk->marks.size() == leg.tiles.size(), "a begun leg's walk table has a mark per tile");
    const std::size_t last = begun - 1;
    const LegWalkMark& mark = walk->marks[last];
    const i64 end = leg.startTick + i64{ leg.arrive[begun] };
    walked.distance = mark.walked + (mark.length * ShareGone(leg.startTick + i64{ leg.arrive[last] }, end, tick));
    walked.moving = mark.length > 0.0 && tick <= end;
    walked.faced = walk->marks[begun].before;
    return walked;
}

/// `stay`'s walk at `tick`, over its days from day point to day point as StayWhere() moves its walker.
PartWalked StayWalked(const WorldMap& map, const Stay& stay, i64 tick) {
    PN_ASSERT(stay.city < map.Places().size(), "a stay is in a place of the map");
    const Vec2 centre = map.TileCentre(map.TileIndex(map.Places()[stay.city].tile));
    PartWalked walked;
    Vec2 from = centre;
    for (i64 day = 0; stay.startTick + (day * DAY_TICKS) < stay.endTick; ++day) {
        const i64 dayStart = stay.startTick + (day * DAY_TICKS);
        if (dayStart >= tick) {
            walked.ahead = static_cast<std::size_t>(day);
            break;
        }
        const i64 dayEnd = std::min(dayStart + DAY_TICKS, stay.endTick);
        const Vec2 target = StayPoint(map, stay, centre, day + 1);
        const Stretch stretch = StretchOf(from, target);
        walked.distance += stretch.length * ShareGone(dayStart, dayEnd, tick);
        walked.moving = walked.moving || (stretch.length > 0.0 && tick <= dayEnd);
        if (stretch.across != Across::None) {
            walked.faced = stretch.across;
        }
        from = target;
    }
    return walked;
}

/// The way the first of `stay`'s days from `firstDay` on that drifts across goes.
Across StayAhead(const WorldMap& map, const Stay& stay, std::size_t firstDay) {
    const Vec2 centre = map.TileCentre(map.TileIndex(map.Places()[stay.city].tile));
    auto day = static_cast<i64>(firstDay);
    Vec2 from = StayPoint(map, stay, centre, day);
    for (; stay.startTick + (day * DAY_TICKS) < stay.endTick; ++day) {
        const Vec2 target = StayPoint(map, stay, centre, day + 1);
        const Across across = StretchOf(from, target).across;
        if (across != Across::None) {
            return across;
        }
        from = target;
    }
    return Across::None;
}

/// Makes each walk table afresh when asked, keeping only the last.
class FreshLegWalks final : public LegWalks {
public:
    explicit FreshLegWalks(const WorldMap& map)
      : m_map{ &map } {
    }

    const LegWalk& Of(const Leg& leg) override {
        m_made = MakeLegWalk(*m_map, leg);
        return m_made;
    }

private:
    const WorldMap* m_map;
    LegWalk m_made;
};

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

bool LegWalk::Fits(const Leg& leg) const {
    return std::bit_cast<u32>(origin.x) == std::bit_cast<u32>(leg.origin.x) &&
           std::bit_cast<u32>(origin.y) == std::bit_cast<u32>(leg.origin.y) && std::ranges::equal(tiles, leg.tiles);
}

LegWalk MakeLegWalk(const WorldMap& map, const Leg& leg) {
    PN_ASSERT(!leg.tiles.empty(), "a leg has a tile to start on");
    LegWalk walk{ .origin = leg.origin, .tiles = leg.tiles, .marks = std::vector<LegWalkMark>(leg.tiles.size()) };
    f64 walked = 0.0;
    Across before = Across::None;
    for (std::size_t index = 0; index < walk.marks.size(); ++index) {
        LegWalkMark& mark = walk.marks[index];
        mark.walked = walked;
        mark.before = before;
        if (index + 1 < walk.marks.size()) {
            const Stretch stretch = StretchOf(LegPoint(map, leg, index), LegPoint(map, leg, index + 1));
            mark.length = stretch.length;
            // The stretch's own way until the pass back below.
            mark.after = stretch.across;
            walked += stretch.length;
            if (stretch.across != Across::None) {
                before = stretch.across;
            }
        }
    }
    Across after = Across::None;
    for (LegWalkMark& mark : std::views::reverse(walk.marks)) {
        if (mark.after != Across::None) {
            after = mark.after;
        }
        mark.after = after;
    }
    return walk;
}

LegWalks::~LegWalks() = default;

Walked WalkedAt(const WorldMap& map, const Journey& journey, i64 tick) {
    FreshLegWalks walks{ map };
    return WalkedAt(map, journey, tick, walks);
}

Walked WalkedAt(const WorldMap& map, const Journey& journey, i64 tick, LegWalks& walks) {
    Walked walked;
    Across faced = Across::None;
    for (const JourneyPart& part : journey.parts) {
        const LegWalk* walk = nullptr;
        PartWalked partWalked;
        if (part.stay) {
            partWalked = StayWalked(map, part.rest, tick);
        }
        else {
            const std::size_t begun = BegunStretches(part.leg, tick);
            if (begun > 0) {
                walk = &walks.Of(part.leg);
            }
            partWalked = LegWalked(part.leg, tick, begun, walk);
        }
        walked.distance += partWalked.distance;
        walked.moving = walked.moving || partWalked.moving;
        if (partWalked.faced != Across::None) {
            faced = partWalked.faced;
        }
        if (!partWalked.ahead) {
            continue;
        }
        // Stretches after one not yet begun are not begun either, so only the facing may still be unknown: a walker that
        // has not yet stepped across faces the way it first will.
        if (faced == Across::None) {
            if (part.stay) {
                faced = StayAhead(map, part.rest, *partWalked.ahead);
            }
            else {
                faced = (walk != nullptr ? *walk : walks.Of(part.leg)).marks[*partWalked.ahead].after;
            }
        }
        if (faced != Across::None) {
            break;
        }
    }
    walked.facesLeft = faced == Across::Left;
    return walked;
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
