#pragma once

/// @file
/// @ingroup core
/// Journeys over the world map: a journey is a record of who travels and its parts in order, legs
/// and stays, decided at departure; where each traveller is at a tick is a pure function of the record, as a sword's
/// place is of its volley. A leg is reached only through Where() and EndTick(), so a new leg shape is one more case
/// there and nothing that uses legs changes. Ticks are signed, so a journey may have left before the clock's tick 0.

#include "core/calendar.hpp"
#include "core/paths.hpp"
#include "core/stable_id.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "core/world_map.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace core {

/// How a leg moves its walker: the one question every shape answers is where the walker is at a tick, and when it ends.
enum class LegShape : u8 {
    TileRoute, ///< From tile centre to tile centre along a route, each step taking its cost in ticks.
};

/// One stretch of a journey, moved by one profile in one shape.
struct Leg {
    /// How it moves its walker.
    LegShape shape = LegShape::TileRoute;
    /// The profile its steps are priced by.
    ProfileIndex profile = WALKING;
    /// The tick it starts on.
    i64 startTick = 0;
    /// Where its walker starts from: the first tile's centre for an NPC, the player's exact foot for a route, so a walker
    /// never jumps to a centre as a leg starts.
    Vec2 origin{};
    /// The route's tiles in order, from the one it starts on; never empty.
    std::vector<u32> tiles;
    /// The ticks from its start to reach each tile, one per tile: 0 for the first, then rising by each step's ticks.
    std::vector<u32> arrive;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.shape, self.profile, self.startTick, self.origin, self.tiles, self.arrive);
    }
};

/// Days (TICKS_PER_DAY ticks each) spent in a city between legs, the walker drifting round its tile.
struct Stay {
    /// The city's place index.
    u16 city = NO_PLACE;
    /// The tick it starts on: the walker is on the city's tile centre then.
    i64 startTick = 0;
    /// The tick it ends on, at or after its start: the walker is back on the tile centre then.
    i64 endTick = 0;
    /// The seed its day points are drawn from (core::Random).
    u32 seed = 0;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.city, self.startTick, self.endTick, self.seed);
    }
};

/// One part of a journey: a leg, or a stay.
struct JourneyPart {
    /// Whether it is a stay (`rest`) rather than a leg (`leg`).
    bool stay = false;
    /// The leg, when it isn't a stay.
    Leg leg{};
    /// The stay, when it is one.
    Stay rest{};

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.stay, self.leg, self.rest);
    }
};

/// Who travels together, and the parts of their journey in order, each starting on the tick the one before ends.
struct Journey {
    /// The travellers, one or more: member 0 walks on the parts' positions, the others at fixed offsets from it.
    std::vector<StableId> members;
    /// The city it sets out from, a place index.
    u16 home = NO_PLACE;
    /// The city it travels to, a place index.
    u16 destination = NO_PLACE;
    /// Its legs and stays in order; never empty.
    std::vector<JourneyPart> parts;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.members, self.home, self.destination, self.parts);
    }
};

/// A tile route leg along `tiles` (at least one, each step between adjacent tiles that `profile` can walk) by `profile`,
/// starting on `startTick` from `origin`: its first step takes the cost of entering `tiles[1]`, each step's ticks being
/// its whole-number cost (WorldMap::StepCost) times the profile's ticks per cost.
[[nodiscard]] Leg MakeTileLeg(
    const WorldMap& map,
    ProfileIndex profile,
    i64 startTick,
    Vec2 origin,
    std::span<const u32> tiles
);

/// A tile route leg along `tiles` (at least one, each step between adjacent tiles that WALKING can walk) paced by
/// distance: `unitsPerTick` world units a tick over the walked length, the origin to the second tile's centre
/// and then centre to centre, a diagonal at its true length. Each arrive tick is the ceiling of the length so far over
/// `unitsPerTick`, summed in floating point from the start so no rounding builds up, and at least a tick after the
/// one before, so a walker's ticks always rise. Empty when `unitsPerTick` isn't above 0 or an arrive tick wouldn't fit
/// in 32 bits.
[[nodiscard]] std::optional<Leg> MakePacedLeg(
    const WorldMap& map,
    i64 startTick,
    Vec2 origin,
    std::span<const u32> tiles,
    f32 unitsPerTick
);

/// The tick `leg` ends on: its walker has reached its last tile.
[[nodiscard]] i64 EndTick(const Leg& leg);

/// The tick `part` starts on: its stay's start or its leg's.
[[nodiscard]] i64 StartTick(const JourneyPart& part);

/// The tick `part` ends on: its stay's end or its leg's (EndTick() of the leg).
[[nodiscard]] i64 EndTick(const JourneyPart& part);

/// The tick `journey` ends on: its last part's end, or 0 for a journey without parts.
[[nodiscard]] i64 EndTick(const Journey& journey);

/// The tick `journey` starts on: its first part's start, or 0 for a journey without parts.
[[nodiscard]] i64 StartTick(const Journey& journey);

/// Where `leg`'s walker is at `tick`: its origin up to its start, its last tile's centre from its end on (the origin for
/// a leg of one tile, which never moves), and between, linear from the point it reached at one tile's arrive tick (the
/// origin for the first) to the next tile's centre.
[[nodiscard]] Vec2 Where(const WorldMap& map, const Leg& leg, i64 tick);

/// Where `member` (an index into its members) of `journey` is at `tick`: the position its part holding `tick` gives (a
/// leg's as Where() of the leg; a stay drifts linearly from one point to the next each day, the points drawn per seed and
/// day within 0.35 of a tile of the city's tile centre and on the centre at its start and end), held at the first part's
/// start before the journey and at the last part's end after it. Member 0 stands on that position, the others round it
/// on a hexagonal lattice 24 units apart, filling its rings outwards (the first six 24 units out, the second ring's twelve
/// 41.6 to 48 units out). Continuous at every part's boundary; nothing for a journey without parts. `member` is below
/// the members' count.
[[nodiscard]] Vec2 Where(const WorldMap& map, const Journey& journey, i64 tick, std::size_t member = 0);

/// How far a journey's walker has walked by a tick, and how it is drawn there.
struct Walked {
    /// The world units walked from the journey's start: each straight stretch's length times the share of its ticks gone,
    /// summed part by part from each part's start and the parts' sums added in order, so a leg's share of it is the same
    /// in every journey that walks that leg.
    f64 distance = 0.0;
    /// Whether it is moving: some stretch of nonzero length holds the tick, after its start and up to its end.
    bool moving = false;
    /// Whether it faces left: it last stepped left (a step straight up or down keeps the facing), or, having not yet
    /// stepped left or right, will first step left.
    bool facesLeft = false;
};

/// Which way a straight stretch goes across the map: left, right, or neither (straight up or down, or nowhere).
enum class Across : u8 {
    None,  ///< Neither way.
    Left,  ///< Towards smaller x.
    Right, ///< Towards larger x.
};

/// What a leg's walk table holds for one of its tiles.
struct LegWalkMark {
    /// The world units of the stretches before the tile, summed in order from the leg's first.
    f64 walked = 0.0;
    /// The length of the stretch from the tile to the next; 0 for the last tile.
    f64 length = 0.0;
    /// The way the last stretch across before the tile goes.
    Across before = Across::None;
    /// The way the first stretch across from the tile on goes.
    Across after = Across::None;
};

/// A leg's walk table: what WalkedAt() needs of a leg's stretches, worked out once from its points (its origin, then its
/// tiles' centres), so how far its walker has gone takes a search of its arrive ticks and one stretch. It depends on the
/// points alone, not on the ticks, so every leg over the same points may share one, whatever its start or profile.
struct LegWalk {
    /// The origin of the leg it was made for.
    Vec2 origin{};
    /// The tiles of the leg it was made for.
    std::vector<u32> tiles;
    /// One per tile, in order.
    std::vector<LegWalkMark> marks;

    /// Whether it is the table of `leg`'s points: the same origin, to the bit, and the same tiles.
    [[nodiscard]] bool Fits(const Leg& leg) const;
};

/// The walk table of `leg`'s points on `map`.
[[nodiscard]] LegWalk MakeLegWalk(const WorldMap& map, const Leg& leg);

/// Where WalkedAt() finds the walk tables of a journey's legs: made afresh, or kept and shared (core::LegWalkCache).
class LegWalks {
public:
    LegWalks() = default;
    LegWalks(const LegWalks&) = delete;
    LegWalks& operator=(const LegWalks&) = delete;
    LegWalks(LegWalks&&) = delete;
    LegWalks& operator=(LegWalks&&) = delete;
    virtual ~LegWalks();

    /// The walk table of `leg`'s points on the map its journey walks (MakeLegWalk()), valid at least until the next call.
    [[nodiscard]] virtual const LegWalk& Of(const Leg& leg) = 0;
};

/// How far member 0 of `journey` has walked at `tick`, whether it moves and which way it faces, over the straight
/// stretches Where() moves it along in order: each leg's steps from tile to tile and each stay's days from day point to
/// day point. The other members keep fixed offsets from it, so they walk alike. A pure function of the journey, the map
/// and the tick, so a paused clock gives the same walk. It makes each leg's walk table it reads afresh, at the cost of
/// walking the leg; a caller asking for many walks keeps the tables and passes them (the overload below).
[[nodiscard]] Walked WalkedAt(const WorldMap& map, const Journey& journey, i64 tick);

/// WalkedAt() above, to the bit, each leg's walk table taken from `walks`: it costs a search of the arrive ticks of each
/// leg begun by `tick`, one stretch of each, and the days of a stay up to `tick`, and asks for no table of a leg not yet
/// begun unless it must look ahead for the facing. It reads no part after the one that settles the facing.
[[nodiscard]] Walked WalkedAt(const WorldMap& map, const Journey& journey, i64 tick, LegWalks& walks);

/// What one journey keeps between the walks WalkedAt() below works out for it, so a journey drawn again and again asks
/// for no table it has had and sums no part already behind it: its first parts' leg tables, each taken once, and how far
/// its finished parts walked and the way they last faced. It holds for one journey on one map and the tables it was given:
/// start it afresh (`{}`) when any of them changes.
struct KeptWalk {
    /// How many of a journey's first parts keep their leg's table: a visit's way out, stay and way home.
    static constexpr std::size_t KEPT_PARTS = 3;

    /// The walk table of each of the first KEPT_PARTS parts' legs, by part: null until asked for, and for a stay.
    std::array<const LegWalk*, KEPT_PARTS> tables{};
    /// The world units walked over the first `finished` parts.
    f64 walked = 0.0;
    /// How many of the parts, from the first, ended before the tick of the last walk.
    u32 finished = 0;
    /// The way the last stretch across in those parts goes.
    Across faced = Across::None;
};

/// WalkedAt() above, to the bit, for the journey `kept` belongs to: a table of one of its first KeptWalk::KEPT_PARTS
/// parts is taken from `walks` only the first time a walk needs it and read from `kept` after, and the parts ended before
/// `tick` are summed into `kept` once (again from the first, should `tick` come before the end of one counted), so a walk
/// costs only the part holding `tick` and any it looks ahead into for the facing. `walks` keeps each table it gives as long
/// as `kept` does (core::LegWalkCache). One thread at a time per `kept`.
[[nodiscard]] Walked WalkedAt(const WorldMap& map, const Journey& journey, i64 tick, KeptWalk& kept, LegWalks& walks);

/// Whether `journey` is underway at `tick`: from its start to the tick before its end.
[[nodiscard]] bool Underway(const Journey& journey, i64 tick);

/// A visit by `member` from city `home` to city `destination` along `out` (from the home's tile to the destination's, each
/// step one `profile`, a profile of `map`, can walk), leaving on `departTick`: the out leg by `profile` from the first
/// tile's centre, a stay of `stayDays` days in the destination drawn from `seed`, and the leg home by `profile` along `out`
/// reversed, its ticks its own entering costs; each part starts where and when the one before ends.
[[nodiscard]] Journey Visit(
    const WorldMap& map,
    std::span<const u32> out,
    ProfileIndex profile,
    StableId member,
    u16 home,
    u16 destination,
    i64 departTick,
    u32 stayDays,
    u32 seed
);

/// What defines a visit: Visit() and the route it walks are a pure function of these and the map, so a save keeps them
/// instead of the legs.
struct VisitPlan {
    /// The travellers, one or more; the first is the one Visit() was made for.
    std::vector<StableId> members;
    /// The city it sets out from, a place index.
    u16 home = NO_PLACE;
    /// The city it travels to, a place index.
    u16 destination = NO_PLACE;
    /// The tile its route starts on, and its way home ends on.
    u32 from = 0;
    /// The tick it leaves on.
    i64 departTick = 0;
    /// The whole days it stays in the destination.
    u32 stayDays = 0;
    /// The seed its stay's day points are drawn from.
    u32 seed = 0;
    /// The profile both its legs move by.
    ProfileIndex profile = WALKING;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.members, self.home, self.destination, self.from, self.departTick, self.stayDays, self.seed, self.profile);
    }
};

/// The VisitPlan `journey` was made from, or none for a journey Visit() didn't make: members, then a leg, a stay in the
/// destination of whole days and the leg home along the first one reversed by the same profile, each starting on the tick
/// the one before ends.
/// What it can't see without a map (the legs' origins and arrive ticks) Rebuild() restores, so a caller that must be sure
/// compares the rebuild with the journey.
[[nodiscard]] std::optional<VisitPlan> PlanOf(const Journey& journey);

/// The journey `plan` describes: the route down `paths`' map by its profile to its destination from its tile (RouteDown()),
/// Visit() of the first member over it by that profile, then the other members appended. A journey without parts when the
/// way is empty: the tile is impassable to the profile, or the destination out of its reach. `plan` has a member, cities
/// of `map`, a tile on it and a profile of it.
[[nodiscard]] Journey Rebuild(const WorldMap& map, Paths& paths, const VisitPlan& plan);

} // namespace core
