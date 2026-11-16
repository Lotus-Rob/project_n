#pragma once

/// @file
/// @ingroup core
/// The world map as data (phase 6 chunk 5b1): the authored terrain grid, roads, rivers, fords and places, and the
/// per-tile layers derived from them (cost, flags, place, region). Read from `world.yaml` by the data module; the
/// scenes, the drawing and the NPC homes read the result. Everything derived is whole-number work with fixed
/// tie-breaks, so it is the same on every machine.

#include "core/error.hpp"
#include "core/rect.hpp"
#include "core/strings.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"

#include <array>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace core {

/// The cost of a tile no one can walk on.
inline constexpr u16 IMPASSABLE_COST = 0xFFFF;
/// The place index of a tile without a place; the region of a tile no city reaches.
inline constexpr u16 NO_PLACE = 0xFFFF;

/// The most any terrain kind, road or ford may cost to walk (a kind that no one can walk on is IMPASSABLE_COST instead):
/// it keeps a path's cost, even a diagonal's seven fifths, far below that sentinel.
inline constexpr u16 MAX_WALK_COST = 1'000;

/// A diagonal step costs this fraction of the cost of the tile it enters, rounded down (about the square root of two):
/// StepCost's rule, which the route search's estimate is built on.
inline constexpr u32 DIAGONAL_NUMERATOR = 7;
/// The denominator of the diagonal step's fraction; see DIAGONAL_NUMERATOR.
inline constexpr u32 DIAGONAL_DENOMINATOR = 5;

/// A way of moving over the map, by its place in WorldMapSource::profiles.
using ProfileIndex = u8;
/// The profile people walk by: every NPC journey and the player's routes use it.
inline constexpr ProfileIndex WALKING = 0;
/// The most movement profiles a map may hold.
inline constexpr std::size_t MOST_PROFILES = 16;
/// The pace of a profile built in code, in ticks per unit of cost.
inline constexpr u16 DEFAULT_TICKS_PER_COST = 16;

/// The most people all the cities of a map may home together: twice the shipped map's 50,000, the tier phase 4's gates
/// were set for. It bounds what the world scene spawns, whatever the data says.
inline constexpr u32 MOST_WORLD_PEOPLE = 100'000;

/// A tile of the map, by column and row.
struct TilePoint {
    /// Its column, from the left.
    u32 column = 0;
    /// Its row, from the top.
    u32 row = 0;

    /// Whether both are equal.
    [[nodiscard]] friend constexpr bool operator==(TilePoint lhs, TilePoint rhs) = default;
};

/// One kind of terrain: its name, the symbol the data writes it with, and what a step onto it costs.
struct TerrainKind {
    /// Its name, as the data and the look name it.
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::string name{};
    /// The symbol its tiles are written with in the grid.
    char symbol = ' ';
    /// The cost of entering one of its tiles, or IMPASSABLE_COST.
    u16 cost = IMPASSABLE_COST;
};

/// The kinds of visitor a city draws, each with a draw of its own.
enum class VisitorKind : u8 {
    Cultivator, ///< A traveller strong enough to be a cultivator (TravelRules::cultivatorPower).
    Trader,     ///< Any other traveller.
};
/// How many kinds of visitor there are.
inline constexpr std::size_t VISITOR_KINDS = 2;
/// The most a city's draw for one kind of visitor may be.
inline constexpr u16 MOST_DRAW = 100;

/// What a place is.
enum class PlaceKind : u8 {
    City,   ///< A settlement: it owns a region and homes NPCs.
    Danger, ///< A hazard the map marks.
};

/// A named spot on the map.
struct Place {
    /// Its key in the data.
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::string key{};
    /// What it is.
    PlaceKind kind = PlaceKind::City;
    /// A danger's sort (a key the data names, not an enum); empty for a city.
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::string sort{};
    /// The tile it stands on.
    TilePoint tile{};
    /// The key of its name in strings.yaml.
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::string nameKey{};
    /// Its name, resolved by the reader: never hashed.
    StringId name{};
    /// How many NPCs a city's share holds; always 0 for a danger, which homes no one.
    u32 people = 0;
    /// Whether the player starts here (exactly one city).
    bool start = false;
    /// How strongly the city draws each kind of visitor (by VisitorKind), each at most MOST_DRAW and not all 0 for a city;
    /// Build zeroes a danger's, which draws no one.
    std::array<u16, VISITOR_KINDS> draws{ 1, 1 };
};

/// The shipped share of a city's people on the road, in parts per thousand.
inline constexpr u16 DEFAULT_SHARE_PER_MILLE = 40;
/// The shipped fewest days a traveller stays.
inline constexpr u8 DEFAULT_STAY_MIN_DAYS = 2;
/// The shipped most days a traveller stays.
inline constexpr u8 DEFAULT_STAY_MAX_DAYS = 5;
/// The shipped power from which a traveller is a cultivator.
inline constexpr f32 DEFAULT_CULTIVATOR_POWER = 5.0F;

/// The rules the travellers between cities follow.
struct TravelRules {
    /// The share of a city's people on the road at once, in parts per thousand; at most 1,000.
    u16 sharePerMille = DEFAULT_SHARE_PER_MILLE;
    /// The fewest days a traveller stays in a city.
    u8 stayMinDays = DEFAULT_STAY_MIN_DAYS;
    /// The most days a traveller stays in a city; at least the fewest, and at least 1.
    u8 stayMaxDays = DEFAULT_STAY_MAX_DAYS;
    /// The base of how far a traveller will go, in cost units.
    u32 reachBase = 0;
    /// What each whole point of power adds to how far a traveller will go.
    u32 reachPerPower = 0;
    /// The power from which a traveller is a cultivator rather than a trader.
    f32 cultivatorPower = DEFAULT_CULTIVATOR_POWER;
};

/// What a profile's steps cost on each feature, each at most MAX_WALK_COST or IMPASSABLE_COST.
struct FeatureCosts {
    /// A road tile's cost, which caps the terrain's.
    u16 road = 0;
    /// A river tile's cost where neither a ford nor a road crosses it.
    u16 river = IMPASSABLE_COST;
    /// A bridge tile's cost, which caps the terrain's.
    u16 bridge = 0;
    /// A ford tile's cost, whatever the terrain.
    u16 ford = 0;
};

/// A way of moving: how long a unit of cost takes and what each terrain kind and feature costs.
struct MovementProfile {
    /// Its key in the data.
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::string key{};
    /// The ticks one unit of cost takes; at least 1.
    u16 ticksPerCost = DEFAULT_TICKS_PER_COST;
    /// The cost of each terrain kind, one per kind, each at most MAX_WALK_COST or IMPASSABLE_COST.
    // NOLINTNEXTLINE(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<u16> kindCosts{};
    /// What the features cost.
    FeatureCosts features{};
};

/// What a tile carries besides its terrain, as bits of FlagsAt().
enum class TileFlag : u8 {
    Road = 1,   ///< A road crosses it.
    River = 2,  ///< A river crosses it.
    Bridge = 4, ///< Road and river cross: the road bridges the river.
    Ford = 8,   ///< A named crossing of a river, walkable at a price.
};

/// The line of the data file each entry of a WorldMapSource was written on, for refusals to name; each vector is
/// indexed like the source's own (kinds, roads, rivers, fords, places), and may be empty or short, as it is for data
/// built in code. Never hashed.
struct SourceLines {
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer for
    // members whose default constructor is defaulted.
    /// The line of each terrain kind.
    std::vector<u32> kinds{};
    /// The line of each road.
    std::vector<u32> roads{};
    /// The line of each river.
    std::vector<u32> rivers{};
    /// The line of each ford.
    std::vector<u32> fords{};
    /// The line of each place.
    std::vector<u32> places{};
    // NOLINTEND(readability-redundant-member-init)
};

/// What the data says, before Build derives the layers.
struct WorldMapSource {
    /// The map's width in tiles.
    u32 columns = 0;
    /// The map's height in tiles.
    u32 rows = 0;
    /// A tile's side in world units.
    f32 tileSize = 0.0F;
    /// The most a road tile costs: it caps the terrain's cost.
    u16 roadCost = 0;
    /// What a ford costs, whatever the terrain.
    u16 fordCost = 0;
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer for
    // members whose default constructor is defaulted.
    /// The terrain kinds.
    std::vector<TerrainKind> kinds{};
    /// One kind index per tile, row by row.
    std::vector<u8> grid{};
    /// The roads, each a polyline of tile points.
    std::vector<std::vector<TilePoint>> roads{};
    /// The rivers, each a polyline of tile points.
    std::vector<std::vector<TilePoint>> rivers{};
    /// The fords: tiles of a river.
    std::vector<TilePoint> fords{};
    /// The places.
    std::vector<Place> places{};
    /// The movement profiles, at most MOST_PROFILES; empty adds `on_foot` from the base costs. Profile WALKING is the
    /// one people walk by.
    std::vector<MovementProfile> profiles{};
    /// The travellers' rules.
    TravelRules travel{};
    /// Where each entry was written, for refusals to name; not hashed.
    SourceLines lines{};
    // NOLINTEND(readability-redundant-member-init)
};

/// A block of tiles, by their first and last columns and rows, both included; empty when its first column or row lies
/// past its last.
struct TileSpan {
    /// Its first column.
    u32 firstColumn = 0;
    /// Its last column.
    u32 lastColumn = 0;
    /// Its first row.
    u32 firstRow = 0;
    /// Its last row.
    u32 lastRow = 0;
};

/// The world map: the authored source and the per-tile layers derived from it.
class WorldMap {
public:
    /// Checks `source` and derives the layers; the error names the kind, line, ford or place it refuses, and the line of
    /// the data it was written on when `source.lines` knows it. Among its refusals: two kinds of one name, a danger with
    /// people, and cities whose people sum past MOST_WORLD_PEOPLE (naming the place that crosses it). A polyline's tiles are
    /// the same whichever way it is written: each segment is stepped from its endpoint with the lower (row, column), one axis
    /// at a time and 4-connected, the column first on a tie.
    [[nodiscard]] static std::expected<WorldMap, Error> Build(WorldMapSource source);

    /// The map's width in tiles.
    [[nodiscard]] u32 Columns() const {
        return m_source.columns;
    }
    /// The map's height in tiles.
    [[nodiscard]] u32 Rows() const {
        return m_source.rows;
    }
    /// A tile's side in world units.
    [[nodiscard]] f32 TileSize() const {
        return m_source.tileSize;
    }
    /// The index of `point`'s tile, row by row.
    [[nodiscard]] u32 TileIndex(TilePoint point) const {
        return (point.row * m_source.columns) + point.column;
    }
    /// The point of the tile with index `tile`.
    [[nodiscard]] TilePoint PointOf(u32 tile) const {
        return { .column = tile % m_source.columns, .row = tile / m_source.columns };
    }
    /// The world position of the middle of `tile`.
    [[nodiscard]] Vec2 TileCentre(u32 tile) const;
    /// The index of the tile `point` lies on (a tile's left and top edges are its own), clamped to the map: a point
    /// before it takes the first column or row, one past it (or not a number) the last.
    [[nodiscard]] u32 TileAt(Vec2 point) const;
    /// The world position of the middle of the start city's tile.
    [[nodiscard]] Vec2 StartCentre() const {
        return TileCentre(TileIndex(m_source.places[m_start].tile));
    }
    /// The terrain kind index of `tile`.
    [[nodiscard]] u8 KindAt(u32 tile) const {
        return m_source.grid.at(tile);
    }
    /// The TileFlag bits of `tile`.
    [[nodiscard]] u8 FlagsAt(u32 tile) const {
        return m_flags.at(tile);
    }
    /// What entering `tile` costs, or IMPASSABLE_COST.
    [[nodiscard]] u16 CostAt(u32 tile) const {
        return m_cost.at(tile);
    }
    /// What entering `tile` costs by `profile`, or IMPASSABLE_COST: a road without a river caps the terrain's cost at the
    /// profile's road cost, a bridge at its bridge cost, a ford costs its ford cost, a river alone its river cost.
    [[nodiscard]] u16 CostAt(u32 tile, ProfileIndex profile) const {
        return m_profileCost.at(profile).at(tile);
    }
    /// The movement profiles, in the source's order (WALKING first).
    [[nodiscard]] std::span<const MovementProfile> Profiles() const {
        return m_source.profiles;
    }
    /// The index of the place on `tile`, or NO_PLACE.
    [[nodiscard]] u16 PlaceAt(u32 tile) const {
        return m_place.at(tile);
    }
    /// The index of the city `tile` is nearest to by walking cost (the lower index on a tie), or NO_PLACE for a tile
    /// no city reaches.
    [[nodiscard]] u16 RegionAt(u32 tile) const {
        return m_region.at(tile);
    }
    /// What a step from `from` onto the adjacent `target` costs: the entered tile's cost; a diagonal costs seven fifths
    /// of it and is allowed only when both orthogonal neighbours are passable. IMPASSABLE_COST when the step isn't
    /// allowed (not adjacent, off the map, or onto an impassable tile); with costs bounded by MAX_WALK_COST, an
    /// allowed step never reaches that value.
    [[nodiscard]] u32 StepCost(u32 from, u32 target) const;
    /// What a step from `from` onto the adjacent `target` costs by `profile`: StepCost's rule over that profile's costs.
    [[nodiscard]] u32 StepCost(u32 from, u32 target, ProfileIndex profile) const;
    /// The passable tiles of the region of place `city`, in tile order; empty for a place that isn't a city.
    [[nodiscard]] std::span<const u32> RegionTiles(u16 city) const;
    /// The places.
    [[nodiscard]] std::span<const Place> Places() const {
        return m_source.places;
    }
    /// The terrain kinds.
    [[nodiscard]] std::span<const TerrainKind> Kinds() const {
        return m_source.kinds;
    }
    /// The roads' polylines.
    [[nodiscard]] std::span<const std::vector<TilePoint>> Roads() const {
        return m_source.roads;
    }
    /// The rivers' polylines.
    [[nodiscard]] std::span<const std::vector<TilePoint>> Rivers() const {
        return m_source.rivers;
    }
    /// The travellers' rules.
    [[nodiscard]] const TravelRules& Travel() const {
        return m_source.travel;
    }
    /// The index of the start city.
    [[nodiscard]] u16 Start() const {
        return m_start;
    }
    /// The tiles `view` overlaps, clamped to the map's; empty when it misses the map.
    [[nodiscard]] TileSpan TilesIn(Rect view) const;
    /// A hash of the authored source: sizes, costs, kinds, grid, lines, fords and the places' keys, kinds, sorts, tiles,
    /// name keys, people, start and draws, the movement profiles and the travel rules; never a place's resolved name.
    [[nodiscard]] u64 Hash() const;

private:
    WorldMap() = default;

    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer for
    // members whose default constructor is defaulted.
    WorldMapSource m_source{};
    std::vector<u8> m_flags{};
    std::vector<u16> m_cost{};
    std::vector<std::vector<u16>> m_profileCost{};
    std::vector<u16> m_place{};
    std::vector<u16> m_region{};
    std::vector<std::vector<u32>> m_regionTiles{};
    // NOLINTEND(readability-redundant-member-init)
    u16 m_start = NO_PLACE;

    [[nodiscard]] u32 StepOver(const std::vector<u16>& costs, u32 from, u32 target) const;
};

} // namespace core
