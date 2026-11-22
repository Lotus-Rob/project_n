#include "core/world_map.hpp"

#include "core/archive.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep

namespace core {

namespace {

constexpr u8 FLAG_ROAD = std::to_underlying(TileFlag::Road);
constexpr u8 FLAG_RIVER = std::to_underlying(TileFlag::River);
constexpr u8 FLAG_BRIDGE = std::to_underlying(TileFlag::Bridge);
constexpr u8 FLAG_FORD = std::to_underlying(TileFlag::Ford);
constexpr u8 FLAG_DANGER = std::to_underlying(TileFlag::Danger);
constexpr f32 TILE_MIDDLE = 0.5F; // the middle of a tile
constexpr u32 MAX_TILES = 1U << 24;

Error Refuse(std::string message) {
    return Error{ .message = std::move(message) };
}

/// The tile `coordinate` lies in along an axis of `tiles`: -1 or `tiles` when it lies before or past the map.
i64 TileOf(f32 coordinate, f32 tileSize, u32 tiles) {
    // fmin and fmax keep a huge or NaN coordinate from overflowing the conversion.
    return static_cast<i64>(std::floor(std::fmax(std::fmin(coordinate / tileSize, static_cast<f32>(tiles)), -1.0F)));
}

void HashLine(Hasher& hasher, const std::vector<TilePoint>& line) {
    hasher(static_cast<u64>(line.size()));
    for (const TilePoint point : line) {
        hasher(point.column, point.row);
    }
}

/// Calls `mark` with every tile a segment between two points passes through, 4-connected, starting from the endpoint
/// with the lower (row, column) so a segment and its reverse mark the same tiles: it steps along the axis whose next
/// tile boundary the segment crosses first, the column's on a tie.
template<typename Mark>
void Rasterise(TilePoint first, TilePoint second, const Mark& mark) {
    const bool swap = std::pair{ second.row, second.column } < std::pair{ first.row, first.column };
    const TilePoint from = swap ? second : first;
    const TilePoint last = swap ? first : second;
    const i64 deltaX = static_cast<i64>(last.column) - static_cast<i64>(from.column);
    const i64 deltaY = static_cast<i64>(last.row) - static_cast<i64>(from.row);
    const i64 stepX = deltaX < 0 ? -1 : 1;
    const i64 stepY = deltaY < 0 ? -1 : 1;
    const i64 lengthX = std::abs(deltaX);
    const i64 lengthY = std::abs(deltaY);
    i64 column = from.column;
    i64 row = from.row;
    i64 doneX = 0;
    i64 doneY = 0;
    mark(TilePoint{ .column = static_cast<u32>(column), .row = static_cast<u32>(row) });
    while (doneX < lengthX || doneY < lengthY) {
        // The segment reaches column boundary doneX + 1/2 and row boundary doneY + 1/2 at these fractions, over a
        // common denominator.
        const i64 acrossX = (((2 * doneX) + 1) * lengthY);
        const i64 acrossY = (((2 * doneY) + 1) * lengthX);
        if (doneY >= lengthY || (doneX < lengthX && acrossX <= acrossY)) {
            column += stepX;
            ++doneX;
        }
        else {
            row += stepY;
            ++doneY;
        }
        mark(TilePoint{ .column = static_cast<u32>(column), .row = static_cast<u32>(row) });
    }
}

/// "line L: " when `lines` knows entry `index`'s line, else nothing.
std::string AtLine(const std::vector<u32>& lines, std::size_t index) {
    return index < lines.size() ? std::format("line {}: ", lines[index]) : std::string{};
}

std::size_t IndexOf(const WorldMapSource& source, TilePoint point) {
    return (static_cast<std::size_t>(point.row) * source.columns) + point.column;
}

struct Seed {
    u16 city;
    u32 tile;
};

struct Reach {
    std::vector<u64> cost;
    std::vector<u16> owner;
};

struct Entry {
    u64 cost;
    u16 city;
    u32 tile;
    [[nodiscard]] friend bool operator>(const Entry& lhs, const Entry& rhs) {
        if (lhs.cost != rhs.cost) {
            return lhs.cost > rhs.cost;
        }
        if (lhs.city != rhs.city) {
            return lhs.city > rhs.city;
        }
        return lhs.tile > rhs.tile;
    }
};

/// One Dijkstra from every seed at cost 0, by StepCost. Ties go to the lower city, then the lower tile, so every tile
/// has one owner whatever the platform.
Reach Search(const WorldMap& map, std::span<const Seed> seeds) {
    const std::size_t tiles = static_cast<std::size_t>(map.Columns()) * map.Rows();
    Reach reach{ .cost = std::vector<u64>(tiles, std::numeric_limits<u64>::max()), .owner = std::vector<u16>(tiles, NO_PLACE) };
    std::vector<bool> settled(tiles, false);
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    for (const Seed seed : seeds) {
        queue.push({ .cost = 0, .city = seed.city, .tile = seed.tile });
    }
    while (!queue.empty()) {
        const Entry entry = queue.top();
        queue.pop();
        if (settled.at(entry.tile)) {
            continue;
        }
        settled.at(entry.tile) = true;
        reach.cost.at(entry.tile) = entry.cost;
        reach.owner.at(entry.tile) = entry.city;
        map.ForEachNeighbour(entry.tile, [&](u32 next) {
            const u32 step = map.StepCost(entry.tile, next);
            if (step != IMPASSABLE_COST && !settled.at(next)) {
                queue.push({ .cost = entry.cost + u64{ step }, .city = entry.city, .tile = next });
            }
        });
    }
    return reach;
}

/// Marks the tiles of every line in `lines` with `flag`; the error names a point off the map, or the first tile `blocked`
/// refuses.
template<typename Blocked>
std::expected<void, Error> MarkLines(
    const WorldMapSource& source,
    std::span<const std::vector<TilePoint>> lines,
    std::string_view what,
    const std::vector<u32>& lineNumbers,
    u8 flag,
    std::vector<u8>& flags,
    const Blocked& blocked
) {
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::vector<TilePoint>& line = lines[index];
        for (std::size_t point = 0; point < line.size(); ++point) {
            if (line[point].column >= source.columns || line[point].row >= source.rows) {
                return std::unexpected{ Refuse(
                    AtLine(lineNumbers, index) +
                    std::format(
                        "{} {}: point {} ({}, {}) lies off the map", what, index, point, line[point].column, line[point].row
                    )
                ) };
            }
        }
        std::optional<Error> refusal;
        for (std::size_t point = 0; point < line.size() && !refusal; ++point) {
            const TilePoint from = line[point];
            const TilePoint last = point + 1 < line.size() ? line[point + 1] : from;
            Rasterise(from, last, [&](TilePoint tile) {
                const std::size_t tileIndex = IndexOf(source, tile);
                flags.at(tileIndex) |= flag;
                if (!refusal && blocked(tileIndex)) {
                    refusal = Refuse(
                        AtLine(lineNumbers, index) + std::format(
                                                         "{} {}: the tile ({}, {}) is {}, which no {} can cross",
                                                         what,
                                                         index,
                                                         tile.column,
                                                         tile.row,
                                                         source.kinds[source.grid[tileIndex]].name,
                                                         what
                                                     )
                    );
                }
            });
        }
        if (refusal) {
            return std::unexpected{ *refusal };
        }
    }
    return {};
}

/// Whether the source's sizes, kinds and grid make a map.
std::expected<void, Error> CheckShape(const WorldMapSource& source) {
    if (source.columns == 0 || source.rows == 0 || static_cast<u64>(source.columns) * source.rows > MAX_TILES) {
        return std::unexpected{ Refuse(std::format("the map is {} by {} tiles", source.columns, source.rows)) };
    }
    if (!(source.tileSize > 0.0F) || !std::isfinite(source.tileSize)) {
        return std::unexpected{ Refuse("the tile size isn't a positive number") };
    }
    const std::size_t tiles = static_cast<std::size_t>(source.columns) * source.rows;
    if (source.grid.size() != tiles) {
        return std::unexpected{ Refuse(std::format("the grid holds {} tiles, not {}", source.grid.size(), tiles)) };
    }
    if (source.kinds.empty() || source.kinds.size() > std::numeric_limits<u8>::max()) {
        return std::unexpected{ Refuse(std::format("the map names {} terrain kinds", source.kinds.size())) };
    }
    for (std::size_t index = 0; index < source.kinds.size(); ++index) {
        const TerrainKind& kind = source.kinds[index];
        // The name is the look's key: two kinds of one name would draw alike.
        const auto twin = std::ranges::find(
            source.kinds.begin(), source.kinds.begin() + static_cast<std::ptrdiff_t>(index), kind.name, &TerrainKind::name
        );
        if (twin != source.kinds.begin() + static_cast<std::ptrdiff_t>(index)) {
            return std::unexpected{ Refuse(
                AtLine(source.lines.kinds, index) +
                std::format("terrain kinds {} and {} share the name '{}'", twin - source.kinds.begin(), index, kind.name)
            ) };
        }
        if (kind.cost == 0) {
            return std::unexpected{ Refuse(
                AtLine(source.lines.kinds, index) +
                std::format("terrain kind {} ('{}') costs 0: a step takes time, so a cost is at least 1", index, kind.name)
            ) };
        }
        if (kind.cost != IMPASSABLE_COST && kind.cost > MAX_WALK_COST) {
            return std::unexpected{ Refuse(
                AtLine(source.lines.kinds, index) + std::format(
                                                        "terrain kind {} ('{}') costs {}, over the most a step may cost, {}",
                                                        index,
                                                        kind.name,
                                                        kind.cost,
                                                        MAX_WALK_COST
                                                    )
            ) };
        }
    }
    if (source.roadCost == 0) {
        return std::unexpected{ Refuse("the road cost is 0: a step takes time, so a cost is at least 1") };
    }
    if (source.fordCost == 0) {
        return std::unexpected{ Refuse("the ford cost is 0: a step takes time, so a cost is at least 1") };
    }
    if (source.roadCost > MAX_WALK_COST || source.fordCost > MAX_WALK_COST) {
        return std::unexpected{ Refuse(
            std::format(
                "the road cost {} or the ford cost {} is over the most a step may cost, {}",
                source.roadCost,
                source.fordCost,
                MAX_WALK_COST
            )
        ) };
    }
    if (source.dangerReach > MAX_DANGER_REACH) {
        return std::unexpected{
            Refuse(std::format("the danger reach {} is over the most, {}", source.dangerReach, MAX_DANGER_REACH))
        };
    }
    if (std::ranges::any_of(source.grid, [&](u8 kind) { return kind >= source.kinds.size(); })) {
        return std::unexpected{ Refuse("the grid names a terrain kind that doesn't exist") };
    }
    if (source.places.size() >= NO_PLACE) {
        return std::unexpected{ Refuse(std::format("the map has {} places", source.places.size())) };
    }
    return {};
}

/// Derives each tile's flags from the lines and the fords; ProfileLayer turns them into costs.
std::expected<void, Error> DeriveTiles(const WorldMapSource& source, std::vector<u8>& flags) {
    const std::size_t tiles = flags.size();
    const auto never = [](std::size_t) { return false; };
    if (auto marked = MarkLines(source, source.rivers, "river", source.lines.rivers, FLAG_RIVER, flags, never); !marked) {
        return marked;
    }
    std::vector<u8> roadFlags(tiles, 0);
    const auto impassable = [&](std::size_t tile) { return source.kinds[source.grid[tile]].cost == IMPASSABLE_COST; };
    if (auto marked = MarkLines(source, source.roads, "road", source.lines.roads, FLAG_ROAD, roadFlags, impassable); !marked) {
        return marked;
    }
    for (std::size_t index = 0; index < source.fords.size(); ++index) {
        const TilePoint ford = source.fords[index];
        const bool onMap = ford.column < source.columns && ford.row < source.rows;
        std::string problem;
        if (!onMap) {
            problem = "lies off the map";
        }
        else if ((flags[IndexOf(source, ford)] & FLAG_RIVER) == 0) {
            problem = "has no river";
        }
        else if (const TerrainKind& kind = source.kinds[source.grid[IndexOf(source, ford)]]; kind.cost == IMPASSABLE_COST) {
            problem = std::format("is {}, which no ford can cross", kind.name);
        }
        else if (roadFlags[IndexOf(source, ford)] != 0) {
            problem = "is bridged";
        }
        if (!problem.empty()) {
            return std::unexpected{ Refuse(
                AtLine(source.lines.fords, index) +
                std::format("ford {}: the tile ({}, {}) {}", index, ford.column, ford.row, problem)
            ) };
        }
        flags[IndexOf(source, ford)] |= FLAG_FORD;
    }
    for (std::size_t tile = 0; tile < tiles; ++tile) {
        if (roadFlags[tile] != 0) {
            flags[tile] |= FLAG_ROAD;
            if ((flags[tile] & FLAG_RIVER) != 0) {
                flags[tile] |= FLAG_BRIDGE;
            }
        }
    }
    return {};
}

/// Whether each profile can be used: at most MOST_PROFILES, each with a key of its own, a pace, one cost per kind, all costs
/// walkable or impassable.
std::expected<void, Error> CheckProfiles(const WorldMapSource& source) {
    if (source.profiles.size() > MOST_PROFILES) {
        return std::unexpected{
            Refuse(std::format("the map has {} profiles, over the most, {}", source.profiles.size(), MOST_PROFILES))
        };
    }
    const auto walkable = [](u16 cost) { return cost == IMPASSABLE_COST || cost <= MAX_WALK_COST; };
    for (std::size_t index = 0; index < source.profiles.size(); ++index) {
        const MovementProfile& profile = source.profiles[index];
        if (profile.key.empty()) {
            return std::unexpected{ Refuse(std::format("profile {} has no key", index)) };
        }
        const auto twin = std::ranges::find(
            source.profiles.begin(),
            source.profiles.begin() + static_cast<std::ptrdiff_t>(index),
            profile.key,
            &MovementProfile::key
        );
        const auto featureCosts =
            std::to_array({ profile.features.road, profile.features.river, profile.features.bridge, profile.features.ford });
        std::string problem;
        if (twin != source.profiles.begin() + static_cast<std::ptrdiff_t>(index)) {
            problem = std::format("shares its key with profile {}", twin - source.profiles.begin());
        }
        else if (profile.ticksPerCost == 0) {
            problem = "takes no ticks per cost";
        }
        else if (profile.kindCosts.size() != source.kinds.size()) {
            problem = std::format("costs {} terrain kinds, not {}", profile.kindCosts.size(), source.kinds.size());
        }
        else if (std::ranges::contains(profile.kindCosts, 0)) {
            problem = "costs a terrain kind 0: a step takes time, so a cost is at least 1";
        }
        else if (std::ranges::contains(featureCosts, 0)) {
            problem = "costs a feature 0: a step takes time, so a cost is at least 1";
        }
        else if (index == WALKING && profile.features.danger != 0) {
            problem = "is everyone's walk, so it adds no danger cost";
        }
        else if (profile.features.danger > MAX_WALK_COST) {
            problem = std::format("adds a danger cost over the most a step may cost, {}", MAX_WALK_COST);
        }
        else if (!std::ranges::all_of(profile.kindCosts, walkable)) {
            problem = std::format("costs a terrain kind over the most a step may cost, {}", MAX_WALK_COST);
        }
        else if (!std::ranges::all_of(featureCosts, walkable)) {
            problem = std::format("costs a feature over the most a step may cost, {}", MAX_WALK_COST);
        }
        if (!problem.empty()) {
            return std::unexpected{ Refuse(std::format("profile {} ('{}') {}", index, profile.key, problem)) };
        }
    }
    return {};
}

/// Whether the bands can be used: at most MOST_PROFILES, the first from power 0, rising strictly, each over a profile that
/// exists among `profileCount`.
std::expected<void, Error> CheckBands(const std::vector<PowerBand>& bands, std::size_t profileCount) {
    if (bands.size() > MOST_PROFILES) {
        return std::unexpected{
            Refuse(std::format("the travel has {} bands, too many bands: at most {}", bands.size(), MOST_PROFILES))
        };
    }
    for (std::size_t index = 0; index < bands.size(); ++index) {
        const PowerBand& band = bands[index];
        // Written so that a NaN fails the first test.
        if (index == 0 && !(band.fromPower == 0.0F)) {
            return std::unexpected{ Refuse(std::format("the first band must start at 0, not {}", band.fromPower)) };
        }
        if (index > 0 && !(band.fromPower > bands[index - 1].fromPower)) {
            return std::unexpected{
                Refuse(std::format("band {} (from {}) doesn't rise past the band before it", index, band.fromPower))
            };
        }
        if (band.profile >= profileCount) {
            return std::unexpected{ Refuse(std::format("band {} names no profile {}", index, band.profile)) };
        }
    }
    return {};
}

/// Whether the travel rules can be used: a share of at most a thousand, a stay of at least a day, in order, bands over the
/// `profileCount` profiles.
std::expected<void, Error> CheckTravel(const TravelRules& travel, std::size_t profileCount) {
    if (travel.sharePerMille > MOST_SHARE_PER_MILLE) {
        return std::unexpected{
            Refuse(std::format("the travel share, {} per thousand, is over a thousand", travel.sharePerMille))
        };
    }
    if (travel.stayMinDays == 0 || travel.stayMinDays > travel.stayMaxDays) {
        return std::unexpected{ Refuse(
            std::format("the stay of {} to {} days isn't 1 day or more, in order", travel.stayMinDays, travel.stayMaxDays)
        ) };
    }
    return CheckBands(travel.bands, profileCount);
}

/// The profile a map without any gets: on foot by the base costs.
MovementProfile OnFoot(const WorldMapSource& source) {
    MovementProfile profile{
        .key = "on_foot",
        .ticksPerCost = DEFAULT_TICKS_PER_COST,
        .kindCosts = {},
        .features = { .road = source.roadCost, .river = IMPASSABLE_COST, .bridge = source.roadCost, .ford = source.fordCost }
    };
    for (const TerrainKind& kind : source.kinds) {
        profile.kindCosts.push_back(kind.cost);
    }
    return profile;
}

/// What entering each tile costs by `profile`, from the tiles' flags.
std::vector<u16> ProfileLayer(const WorldMapSource& source, const std::vector<u8>& flags, const MovementProfile& profile) {
    std::vector<u16> cost(flags.size(), 0);
    for (std::size_t tile = 0; tile < flags.size(); ++tile) {
        const u16 kindCost = profile.kindCosts[source.grid[tile]];
        const u8 tileFlags = flags[tile];
        if ((tileFlags & FLAG_BRIDGE) != 0) {
            cost[tile] = std::min(kindCost, profile.features.bridge);
        }
        else if ((tileFlags & FLAG_FORD) != 0) {
            cost[tile] = profile.features.ford;
        }
        else if ((tileFlags & FLAG_RIVER) != 0) {
            cost[tile] = profile.features.river;
        }
        else if ((tileFlags & FLAG_ROAD) != 0) {
            cost[tile] = std::min(kindCost, profile.features.road);
        }
        else {
            cost[tile] = kindCost;
        }
        if ((tileFlags & FLAG_DANGER) != 0 && cost[tile] != IMPASSABLE_COST) {
            const u32 priced = u32{ cost[tile] } + u32{ profile.features.danger };
            cost[tile] = static_cast<u16>(std::min<u32>(priced, MAX_WALK_COST));
        }
    }
    return cost;
}

/// Flags every tile whose centre lies within the danger reach of a danger place's tile. Places must already be on the map.
void MarkDanger(const WorldMapSource& source, std::vector<u8>& flags) {
    const i64 reach = source.dangerReach;
    for (const Place& place : source.places) {
        if (place.kind != PlaceKind::Danger || reach == 0) {
            continue;
        }
        const i64 firstRow = std::max<i64>(0, i64{ place.tile.row } - reach);
        const i64 lastRow = std::min<i64>(source.rows - 1, i64{ place.tile.row } + reach);
        const i64 firstColumn = std::max<i64>(0, i64{ place.tile.column } - reach);
        const i64 lastColumn = std::min<i64>(source.columns - 1, i64{ place.tile.column } + reach);
        for (i64 row = firstRow; row <= lastRow; ++row) {
            for (i64 column = firstColumn; column <= lastColumn; ++column) {
                const i64 across = column - i64{ place.tile.column };
                const i64 down = row - i64{ place.tile.row };
                if (((across * across) + (down * down)) <= (reach * reach)) {
                    flags[(static_cast<std::size_t>(row) * source.columns) + static_cast<std::size_t>(column)] |= FLAG_DANGER;
                }
            }
        }
    }
}

struct PlaceLayer {
    std::vector<u16> place;
    u16 start = NO_PLACE;
};

/// Whether place `index`'s kind suits what it says: a sort only for a danger, the start only for a city, people only for a
/// city (a danger's would be read and never spawned).
std::expected<void, Error> CheckRole(const WorldMapSource& source, std::size_t index) {
    const Place& place = source.places[index];
    std::string problem;
    if ((place.kind == PlaceKind::City) != place.sort.empty()) {
        problem = place.sort.empty() ? "is a danger without a sort" : "is a city with a sort";
    }
    else if (place.start && place.kind != PlaceKind::City) {
        problem = "starts the game but isn't a city";
    }
    else if (place.kind == PlaceKind::Danger && place.people > 0) {
        problem = "is a danger with people";
    }
    else if (std::ranges::any_of(place.draws, [](u16 draw) { return draw > MOST_DRAW; })) {
        problem = std::format("draws visitors past the most, {}", MOST_DRAW);
    }
    else if (place.kind == PlaceKind::City && std::ranges::all_of(place.draws, [](u16 draw) { return draw == 0; })) {
        problem = "is a city that draws no visitors";
    }
    if (!problem.empty()) {
        return std::unexpected{
            Refuse(AtLine(source.lines.places, index) + std::format("place {} ('{}') {}", index, place.key, problem))
        };
    }
    return {};
}

/// Puts the places on their tiles, each on a walkable tile of its own, and finds the one start.
std::expected<PlaceLayer, Error> PlaceTiles(const WorldMapSource& source, const std::vector<u16>& cost) {
    PlaceLayer layer{ .place = std::vector<u16>(cost.size(), NO_PLACE), .start = NO_PLACE };
    u64 people = 0;
    for (std::size_t index = 0; index < source.places.size(); ++index) {
        const Place& place = source.places[index];
        const auto twin = std::ranges::find(
            source.places.begin(), source.places.begin() + static_cast<std::ptrdiff_t>(index), place.key, &Place::key
        );
        if (twin != source.places.begin() + static_cast<std::ptrdiff_t>(index)) {
            return std::unexpected{ Refuse(
                AtLine(source.lines.places, index) +
                std::format("places {} and {} share the key '{}'", twin - source.places.begin(), index, place.key)
            ) };
        }
        if (place.tile.column >= source.columns || place.tile.row >= source.rows) {
            return std::unexpected{
                Refuse(AtLine(source.lines.places, index) + std::format("place {} ('{}') lies off the map", index, place.key))
            };
        }
        const std::size_t tile = IndexOf(source, place.tile);
        if (cost[tile] == IMPASSABLE_COST) {
            return std::unexpected{ Refuse(
                AtLine(source.lines.places, index) +
                std::format("place {} ('{}') stands on a tile no one can walk on", index, place.key)
            ) };
        }
        if (layer.place[tile] != NO_PLACE) {
            return std::unexpected{ Refuse(
                AtLine(source.lines.places, index) + std::format(
                                                         "place {} ('{}') shares its tile with place {} ('{}')",
                                                         index,
                                                         place.key,
                                                         layer.place[tile],
                                                         source.places[layer.place[tile]].key
                                                     )
            ) };
        }
        if (auto role = CheckRole(source, index); !role) {
            return std::unexpected{ role.error() };
        }
        people += place.people;
        if (people > MOST_WORLD_PEOPLE) {
            return std::unexpected{ Refuse(
                AtLine(source.lines.places, index) + std::format(
                                                         "place {} ('{}') brings the cities' people to {}, over the most a "
                                                         "world holds, {}",
                                                         index,
                                                         place.key,
                                                         people,
                                                         MOST_WORLD_PEOPLE
                                                     )
            ) };
        }
        layer.place[tile] = static_cast<u16>(index);
        if (!place.start) {
            continue;
        }
        if (layer.start != NO_PLACE) {
            return std::unexpected{ Refuse(
                AtLine(source.lines.places, index) + std::format("places {} and {} both start the game", layer.start, index)
            ) };
        }
        layer.start = static_cast<u16>(index);
    }
    if (layer.start == NO_PLACE) {
        return std::unexpected{ Refuse("no place starts the game") };
    }
    return layer;
}

} // namespace

std::expected<WorldMap, Error> WorldMap::WithTravel(const TravelRules& travel) const {
    if (auto checked = CheckTravel(travel, m_source.profiles.size()); !checked) {
        return std::unexpected{ checked.error() };
    }
    WorldMap map = *this;
    map.m_source.travel = travel;
    return map;
}

std::expected<WorldMap, Error> WorldMap::Build(WorldMapSource source) {
    if (auto shape = CheckShape(source); !shape) {
        return std::unexpected{ shape.error() };
    }
    if (auto profiles = CheckProfiles(source); !profiles) {
        return std::unexpected{ profiles.error() };
    }
    if (source.profiles.empty()) {
        source.profiles.push_back(OnFoot(source));
    }
    if (auto travel = CheckTravel(source.travel, source.profiles.size()); !travel) {
        return std::unexpected{ travel.error() };
    }
    const std::size_t tiles = source.grid.size();
    WorldMap map;
    map.m_flags.assign(tiles, 0);
    if (auto derived = DeriveTiles(source, map.m_flags); !derived) {
        return std::unexpected{ derived.error() };
    }
    map.m_cost = ProfileLayer(source, map.m_flags, OnFoot(source));
    auto placed = PlaceTiles(source, map.m_cost);
    if (!placed) {
        return std::unexpected{ placed.error() };
    }
    // Danger flags need the places checked and on the map, and only the profile layers (never the base layer) price them.
    MarkDanger(source, map.m_flags);
    for (const MovementProfile& profile : source.profiles) {
        map.m_profileCost.push_back(ProfileLayer(source, map.m_flags, profile));
    }
    // A danger draws no one, whatever a source built in code left in it.
    for (Place& place : source.places) {
        if (place.kind == PlaceKind::Danger) {
            place.draws.fill(0);
        }
    }
    map.m_place = std::move(placed->place);
    map.m_start = placed->start;
    map.m_source = std::move(source);

    std::vector<Seed> cities;
    for (std::size_t index = 0; index < map.m_source.places.size(); ++index) {
        if (map.m_source.places[index].kind == PlaceKind::City) {
            cities.push_back({ .city = static_cast<u16>(index), .tile = map.TileIndex(map.m_source.places[index].tile) });
        }
    }
    const Seed start{ .city = map.m_start, .tile = map.TileIndex(map.m_source.places[map.m_start].tile) };
    const Reach fromStart = Search(map, std::span{ &start, 1 });
    for (const Seed city : cities) {
        if (fromStart.owner[city.tile] == NO_PLACE) {
            return std::unexpected{ Refuse(
                AtLine(map.m_source.lines.places, city.city) +
                std::format(
                    "place {} ('{}') can't be reached on foot from the start", city.city, map.m_source.places[city.city].key
                )
            ) };
        }
    }

    map.m_region = Search(map, cities).owner;
    map.m_regionTiles.assign(map.m_source.places.size(), {});
    for (std::size_t tile = 0; tile < tiles; ++tile) {
        if (map.m_region[tile] != NO_PLACE) {
            map.m_regionTiles[map.m_region[tile]].push_back(static_cast<u32>(tile));
        }
    }
    return map;
}

Vec2 WorldMap::TileCentre(u32 tile) const {
    const TilePoint point = PointOf(tile);
    return { .x = (static_cast<f32>(point.column) + TILE_MIDDLE) * m_source.tileSize,
             .y = (static_cast<f32>(point.row) + TILE_MIDDLE) * m_source.tileSize };
}

u32 WorldMap::StepCost(u32 from, u32 target) const {
    return StepOver(m_cost, from, target);
}

u32 WorldMap::StepCost(u32 from, u32 target, ProfileIndex profile) const {
    return StepOver(m_profileCost.at(profile), from, target);
}

u32 WorldMap::StepOver(const std::vector<u16>& costs, u32 from, u32 target) const {
    const u64 tiles = static_cast<u64>(m_source.columns) * m_source.rows;
    if (from >= tiles || target >= tiles || from == target) {
        return IMPASSABLE_COST;
    }
    const TilePoint start = PointOf(from);
    const TilePoint end = PointOf(target);
    const i64 dColumn = static_cast<i64>(end.column) - static_cast<i64>(start.column);
    const i64 dRow = static_cast<i64>(end.row) - static_cast<i64>(start.row);
    if (std::abs(dColumn) > 1 || std::abs(dRow) > 1) {
        return IMPASSABLE_COST;
    }
    const u32 cost = costs[target];
    if (cost == IMPASSABLE_COST) {
        return IMPASSABLE_COST;
    }
    if (dColumn == 0 || dRow == 0) {
        return cost;
    }
    const u32 alongRow = TileIndex({ .column = end.column, .row = start.row });
    const u32 alongColumn = TileIndex({ .column = start.column, .row = end.row });
    if (costs[alongRow] == IMPASSABLE_COST || costs[alongColumn] == IMPASSABLE_COST) {
        return IMPASSABLE_COST;
    }
    return cost * DIAGONAL_NUMERATOR / DIAGONAL_DENOMINATOR;
}

u32 WorldMap::TileAt(Vec2 point) const {
    const auto clamped = [this](f32 coordinate, u32 tiles) {
        return static_cast<u32>(std::clamp<i64>(TileOf(coordinate, m_source.tileSize, tiles), 0, static_cast<i64>(tiles) - 1));
    };
    return TileIndex({ .column = clamped(point.x, m_source.columns), .row = clamped(point.y, m_source.rows) });
}

std::span<const u32> WorldMap::RegionTiles(u16 city) const {
    if (city >= m_regionTiles.size()) {
        return {};
    }
    return m_regionTiles[city];
}

TileSpan WorldMap::TilesIn(Rect view) const {
    const i64 firstColumn = std::max<i64>(TileOf(view.min.x, m_source.tileSize, m_source.columns), 0);
    const i64 lastColumn =
        std::min<i64>(TileOf(view.max.x, m_source.tileSize, m_source.columns), static_cast<i64>(m_source.columns) - 1);
    const i64 firstRow = std::max<i64>(TileOf(view.min.y, m_source.tileSize, m_source.rows), 0);
    const i64 lastRow =
        std::min<i64>(TileOf(view.max.y, m_source.tileSize, m_source.rows), static_cast<i64>(m_source.rows) - 1);
    if (firstColumn > lastColumn || firstRow > lastRow) {
        return { .firstColumn = 1, .lastColumn = 0, .firstRow = 1, .lastRow = 0 };
    }
    return { .firstColumn = static_cast<u32>(firstColumn),
             .lastColumn = static_cast<u32>(lastColumn),
             .firstRow = static_cast<u32>(firstRow),
             .lastRow = static_cast<u32>(lastRow) };
}

u64 WorldMap::Hash() const {
    Hasher hasher;
    hasher(m_source.columns, m_source.rows, m_source.tileSize, m_source.roadCost, m_source.fordCost, m_source.dangerReach);
    hasher(static_cast<u64>(m_source.kinds.size()));
    for (const TerrainKind& kind : m_source.kinds) {
        HashText(hasher, kind.name);
        hasher(kind.symbol, kind.cost);
    }
    for (const u8 kind : m_source.grid) {
        hasher(kind);
    }
    hasher(static_cast<u64>(m_source.profiles.size()));
    for (const MovementProfile& profile : m_source.profiles) {
        HashText(hasher, profile.key);
        hasher(
            profile.ticksPerCost,
            profile.features.road,
            profile.features.river,
            profile.features.bridge,
            profile.features.ford,
            profile.features.danger
        );
        hasher(static_cast<u64>(profile.kindCosts.size()));
        for (const u16 cost : profile.kindCosts) {
            hasher(cost);
        }
    }
    for (const auto* lines : { &m_source.roads, &m_source.rivers }) {
        hasher(static_cast<u64>(lines->size()));
        for (const std::vector<TilePoint>& line : *lines) {
            HashLine(hasher, line);
        }
    }
    HashLine(hasher, m_source.fords);
    hasher(static_cast<u64>(m_source.places.size()));
    for (const Place& place : m_source.places) {
        HashText(hasher, place.key);
        hasher(place.kind);
        HashText(hasher, place.sort);
        hasher(place.tile.column, place.tile.row);
        HashText(hasher, place.nameKey);
        hasher(place.people, place.start);
        for (const u16 draw : place.draws) {
            hasher(draw);
        }
    }
    const TravelRules& travel = m_source.travel;
    hasher(
        travel.sharePerMille,
        travel.stayMinDays,
        travel.stayMaxDays,
        travel.reachBase,
        travel.reachPerPower,
        travel.cultivatorPower
    );
    hasher(static_cast<u64>(travel.bands.size()));
    for (const PowerBand& band : travel.bands) {
        hasher(band.fromPower, band.profile);
    }
    return hasher.Value();
}

} // namespace core
