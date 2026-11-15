#include "data/world_map.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "data/internal/yaml.hpp"

namespace data {

namespace {

constexpr std::string_view WORLD_READER = "world";

// The first character past the visible ASCII ones.
constexpr char DELETE = 0x7F;
// How many values a character takes.
constexpr std::size_t CHARACTERS = std::size_t{ std::numeric_limits<u8>::max() } + 1;
// The most a tile coordinate or a place's people may be: far past any map or city, well within a u32.
constexpr i64 MOST_COORDINATE = 1'000'000;
constexpr i64 MOST_PEOPLE = 100'000'000;
// The least and the most a tile's side may be, in world units: it keeps every position finite and nonzero in an f32.
constexpr f64 LEAST_TILE_SIZE = 1.0;
constexpr f64 MOST_TILE_SIZE = 1'000'000.0;

using Failure = std::unexpected<core::Error>;

Failure RefuseMap(const YAML::Node& node, std::string_view why) {
    return Failure{ internal::RefusalAt(WORLD_READER, node, why) };
}

// The 1-based line `node` was written on, as the other readers report it.
u32 LineOf(const YAML::Node& node) {
    return static_cast<u32>(node.Mark().line) + 1;
}

// The whole number from 0 to `limit` that `node` holds, which `owner` names in refusals.
std::expected<i64, core::Error> Whole(const YAML::Node& node, std::string_view owner, i64 limit) {
    f64 value = 0.0;
    try {
        value = node.as<f64>();
    }
    catch (const YAML::Exception&) {
        return RefuseMap(node, std::format("{} isn't a number", owner));
    }
    if (!std::isfinite(value) || value < 0.0 || value != std::floor(value) || value > static_cast<f64>(limit)) {
        return RefuseMap(node, std::format("{} isn't a whole number from 0 to {}", owner, limit));
    }
    return static_cast<i64>(value);
}

// The tile `node` holds as [column, row].
std::expected<core::TilePoint, core::Error> Point(const YAML::Node& node, std::string_view owner) {
    if (!node.IsSequence() || node.size() != 2) {
        return RefuseMap(node, std::format("{} has a point that isn't [column, row]", owner));
    }
    const std::expected<i64, core::Error> column = Whole(node[0], std::format("{}'s column", owner), MOST_COORDINATE);
    if (!column) {
        return std::unexpected{ column.error() };
    }
    const std::expected<i64, core::Error> row = Whole(node[1], std::format("{}'s row", owner), MOST_COORDINATE);
    if (!row) {
        return std::unexpected{ row.error() };
    }
    return core::TilePoint{ .column = static_cast<u32>(*column), .row = static_cast<u32>(*row) };
}

// The sequence at `key` in `root` (empty when absent), or a refusal when it isn't one.
std::expected<YAML::Node, core::Error> Sequence(const YAML::Node& root, const char* key) {
    const YAML::Node node = root[key];
    if (node && !node.IsSequence()) {
        return RefuseMap(node, std::format("{} isn't a list", key));
    }
    return std::expected<YAML::Node, core::Error>{ node };
}

// The text at `key` in `map`, which `owner` names in refusals.
std::expected<std::string, core::Error> Text(const YAML::Node& map, const char* key, std::string_view owner) {
    const YAML::Node node = map[key];
    if (!node) {
        return RefuseMap(map, std::format("{} has no {}", owner, key));
    }
    if (!node.IsScalar() || node.Scalar().empty()) {
        return RefuseMap(node, std::format("{}'s {} isn't text", owner, key));
    }
    return node.Scalar();
}

std::expected<void, core::Error> ReadKinds(
    const YAML::Node& root,
    core::WorldMapSource& source,
    std::vector<std::string>* warnings
) {
    const std::expected<YAML::Node, core::Error> kinds = Sequence(root, "kinds");
    if (!kinds) {
        return std::unexpected{ kinds.error() };
    }
    if (!*kinds) {
        return RefuseMap(root, "there are no kinds");
    }
    for (const YAML::Node& entry : *kinds) {
        if (!entry.IsMap()) {
            return RefuseMap(entry, "a kind isn't a map");
        }
        if (source.kinds.size() >= std::numeric_limits<u8>::max()) {
            return RefuseMap(entry, std::format("too many kinds: at most {}", std::numeric_limits<u8>::max()));
        }
        const std::expected<std::string, core::Error> name = Text(entry, "name", "a kind");
        if (!name) {
            return std::unexpected{ name.error() };
        }
        internal::NoteUnknownKeys(
            WORLD_READER, entry, std::format("kind {}", *name), "key", { "symbol", "name", "cost" }, warnings
        );
        const std::expected<std::string, core::Error> symbol = Text(entry, "symbol", std::format("kind {}", *name));
        if (!symbol) {
            return std::unexpected{ symbol.error() };
        }
        // A visible ASCII character: a space or a tab can't be told apart in a grid.
        if (symbol->size() != 1 || (*symbol)[0] <= ' ' || (*symbol)[0] >= DELETE) {
            return RefuseMap(entry["symbol"], std::format("kind {}'s symbol isn't one visible ASCII character", *name));
        }
        if (std::ranges::any_of(source.kinds, [&](const core::TerrainKind& kind) { return kind.symbol == (*symbol)[0]; })) {
            return RefuseMap(entry, std::format("kind {}'s symbol '{}' is another kind's too", *name, *symbol));
        }
        const YAML::Node cost = entry["cost"];
        if (!cost) {
            return RefuseMap(entry, std::format("kind {} has no cost", *name));
        }
        u16 walk = core::IMPASSABLE_COST;
        if (!cost.IsScalar() || cost.Scalar() != "impassable") {
            const std::expected<i64, core::Error> whole =
                Whole(cost, std::format("kind {}'s cost", *name), core::IMPASSABLE_COST - 1);
            if (!whole) {
                return std::unexpected{ whole.error() };
            }
            walk = static_cast<u16>(*whole);
        }
        source.kinds.push_back({ .name = *name, .symbol = (*symbol)[0], .cost = walk });
        source.lines.kinds.push_back(LineOf(entry));
    }
    return {};
}

std::expected<void, core::Error> ReadGrid(const YAML::Node& root, std::string_view yaml, core::WorldMapSource& source) {
    const YAML::Node grid = root["grid"];
    if (!grid) {
        return RefuseMap(root, "there is no grid");
    }
    if (!grid.IsScalar()) {
        return RefuseMap(grid, "the grid isn't a block of rows");
    }
    // Only a literal block scalar (`|`) keeps one row to a line, and its rows' lines are counted from the indicator, which
    // yaml-cpp's mark points at: a folded (`>`) or quoted grid would be misread, so it is refused.
    const auto indicator = static_cast<std::size_t>(grid.Mark().pos);
    if (indicator >= yaml.size() || yaml[indicator] != '|') {
        return RefuseMap(grid, "the grid isn't a literal block (`grid: |`, one row to a line)");
    }
    // The rows of a block scalar (`|`) start on the line after its indicator.
    const u32 firstRow = LineOf(grid) + 1;
    std::vector<std::string_view> rows;
    std::string_view rest = grid.Scalar();
    for (bool more = true; more;) {
        const std::size_t end = rest.find('\n');
        more = end != std::string_view::npos;
        std::string_view text = rest.substr(0, end);
        rest.remove_prefix(more ? end + 1 : rest.size());
        while (text.ends_with('\r') || text.ends_with(' ')) {
            text.remove_suffix(1);
        }
        rows.push_back(text);
    }
    while (!rows.empty() && rows.back().empty()) {
        rows.pop_back();
    }
    if (rows.empty()) {
        return RefuseMap(grid, "the grid has no rows");
    }
    std::array<u8, CHARACTERS> kindOf{};
    std::array<bool, CHARACTERS> known{};
    for (std::size_t kind = 0; kind < source.kinds.size(); ++kind) {
        const auto symbol = static_cast<u8>(source.kinds[kind].symbol);
        kindOf.at(symbol) = static_cast<u8>(kind);
        known.at(symbol) = true;
    }
    source.columns = static_cast<u32>(rows.front().size());
    source.rows = static_cast<u32>(rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const u32 line = firstRow + static_cast<u32>(index);
        if (rows[index].empty()) {
            return Failure{ core::Error{ std::format("{} line {}: grid row {} is empty", WORLD_READER, line, index + 1) } };
        }
        if (rows[index].size() != rows.front().size()) {
            return Failure{ core::Error{ std::format(
                "{} line {}: grid row {} holds {} tiles, not {} like the first",
                WORLD_READER,
                line,
                index + 1,
                rows[index].size(),
                rows.front().size()
            ) } };
        }
        for (std::size_t column = 0; column < rows[index].size(); ++column) {
            const auto symbol = static_cast<u8>(rows[index][column]);
            if (!known.at(symbol)) {
                return Failure{ core::Error{ std::format(
                    "{} line {}: grid row {}, column {} holds {:?}, which no kind uses",
                    WORLD_READER,
                    line,
                    index + 1,
                    column + 1,
                    static_cast<char>(symbol)
                ) } };
            }
            source.grid.push_back(kindOf.at(symbol));
        }
    }
    return {};
}

// Reads each polyline of the list `key` into `lines`, noting the line of each.
std::expected<void, core::Error> ReadPolylines(
    const YAML::Node& root,
    const char* key,
    std::vector<std::vector<core::TilePoint>>& lines,
    std::vector<u32>& written
) {
    const std::expected<YAML::Node, core::Error> list = Sequence(root, key);
    if (!list) {
        return std::unexpected{ list.error() };
    }
    for (const YAML::Node& entry : *list) {
        if (!entry.IsSequence()) {
            return RefuseMap(entry, std::format("a {} isn't a list of points", key));
        }
        std::vector<core::TilePoint> line;
        for (const YAML::Node& point : entry) {
            const std::expected<core::TilePoint, core::Error> tile = Point(point, key);
            if (!tile) {
                return std::unexpected{ tile.error() };
            }
            line.push_back(*tile);
        }
        lines.push_back(std::move(line));
        written.push_back(LineOf(entry));
    }
    return {};
}

std::expected<void, core::Error> ReadFords(const YAML::Node& root, core::WorldMapSource& source) {
    const std::expected<YAML::Node, core::Error> list = Sequence(root, "fords");
    if (!list) {
        return std::unexpected{ list.error() };
    }
    for (const YAML::Node& entry : *list) {
        const std::expected<core::TilePoint, core::Error> tile = Point(entry, "a ford");
        if (!tile) {
            return std::unexpected{ tile.error() };
        }
        source.fords.push_back(*tile);
        source.lines.fords.push_back(LineOf(entry));
    }
    return {};
}

// Reads the kind of `entry`, a place called `owner`, into `place`.
std::expected<void, core::Error> ReadPlaceKind(const YAML::Node& entry, const std::string& owner, core::Place& place) {
    const std::expected<std::string, core::Error> kind = Text(entry, "kind", owner);
    if (!kind) {
        return std::unexpected{ kind.error() };
    }
    if (*kind == "city") {
        place.kind = core::PlaceKind::City;
    }
    else if (*kind == "danger") {
        place.kind = core::PlaceKind::Danger;
    }
    else {
        return RefuseMap(entry["kind"], std::format("{}'s kind \"{}\" is neither city nor danger", owner, *kind));
    }
    if (const YAML::Node sort = entry["sort"]) {
        if (!sort.IsScalar()) {
            return RefuseMap(sort, std::format("{}'s sort isn't text", owner));
        }
        place.sort = sort.Scalar();
    }
    return {};
}

// Reads the name of `entry`, a place called `owner`, into `place`, looking its string up.
std::expected<void, core::Error> ReadPlaceName(
    const YAML::Node& entry,
    const std::string& owner,
    const core::Strings& strings,
    core::Place& place
) {
    const std::expected<std::string, core::Error> name = Text(entry, "name", owner);
    if (!name) {
        return std::unexpected{ name.error() };
    }
    const std::optional<core::StringId> found = strings.Find(*name);
    if (!found) {
        return RefuseMap(entry["name"], std::format("{}'s name \"{}\" has no string in strings.yaml", owner, *name));
    }
    place.nameKey = *name;
    place.name = *found;
    return {};
}

// Reads the tile, the people and whether it starts of `entry`, a place called `owner`, into `place`.
std::expected<void, core::Error> ReadPlaceTile(const YAML::Node& entry, const std::string& owner, core::Place& place) {
    if (!entry["tile"]) {
        return RefuseMap(entry, std::format("{} has no tile", owner));
    }
    const std::expected<core::TilePoint, core::Error> tile = Point(entry["tile"], owner);
    if (!tile) {
        return std::unexpected{ tile.error() };
    }
    place.tile = *tile;
    if (const YAML::Node people = entry["people"]) {
        const std::expected<i64, core::Error> count = Whole(people, std::format("{}'s people", owner), MOST_PEOPLE);
        if (!count) {
            return std::unexpected{ count.error() };
        }
        place.people = static_cast<u32>(*count);
    }
    if (const YAML::Node start = entry["start"]) {
        try {
            place.start = start.as<bool>();
        }
        catch (const YAML::Exception&) {
            return RefuseMap(start, std::format("{}'s start isn't true or false", owner));
        }
    }
    return {};
}

std::expected<void, core::Error> ReadPlaces(
    const YAML::Node& root,
    const core::Strings& strings,
    core::WorldMapSource& source,
    std::vector<std::string>* warnings
) {
    const std::expected<YAML::Node, core::Error> list = Sequence(root, "places");
    if (!list) {
        return std::unexpected{ list.error() };
    }
    for (const YAML::Node& entry : *list) {
        if (!entry.IsMap()) {
            return RefuseMap(entry, "a place isn't a map");
        }
        core::Place place;
        const std::expected<std::string, core::Error> key = Text(entry, "key", "a place");
        if (!key) {
            return std::unexpected{ key.error() };
        }
        place.key = *key;
        const std::string owner = std::format("place {}", place.key);
        internal::NoteUnknownKeys(
            WORLD_READER, entry, owner, "key", { "key", "kind", "sort", "tile", "name", "people", "start" }, warnings
        );
        if (const std::expected<void, core::Error> read =
                ReadPlaceKind(entry, owner, place).and_then([&] { return ReadPlaceTile(entry, owner, place); }).and_then([&] {
                    return ReadPlaceName(entry, owner, strings, place);
                });
            !read) {
            return read;
        }
        source.places.push_back(std::move(place));
        source.lines.places.push_back(LineOf(entry));
    }
    return {};
}

// Reads the costs and the tile size into `source`. A cost over core::MAX_WALK_COST is refused here, on its own key's line:
// Build refuses the same bound (CheckShape) but, with no line for these two, without one, so the two must agree.
std::expected<void, core::Error> ReadSizes(const YAML::Node& root, core::WorldMapSource& source) {
    const std::expected<i64, core::Error> road =
        internal::WholeNumber(WORLD_READER, root, "the map", "road_cost", true, core::MAX_WALK_COST);
    if (!road) {
        return std::unexpected{ road.error() };
    }
    const std::expected<i64, core::Error> ford =
        internal::WholeNumber(WORLD_READER, root, "the map", "ford_cost", true, core::MAX_WALK_COST);
    if (!ford) {
        return std::unexpected{ ford.error() };
    }
    const std::expected<f64, core::Error> tile = internal::RequiredNumber(
        WORLD_READER, root, "the map", "tile_size", { .low = LEAST_TILE_SIZE, .lowOpen = false, .high = MOST_TILE_SIZE }
    );
    if (!tile) {
        return std::unexpected{ tile.error() };
    }
    source.roadCost = static_cast<u16>(*road);
    source.fordCost = static_cast<u16>(*ford);
    source.tileSize = static_cast<f32>(*tile);
    return {};
}

} // namespace

std::expected<core::WorldMap, core::Error> ParseWorldMap(
    std::string_view yaml,
    const core::Strings& strings,
    std::vector<std::string>* warnings
) {
    try {
        const YAML::Node root = YAML::Load(std::string{ yaml });
        if (!root.IsMap()) {
            return RefuseMap(root, "not a map");
        }
        internal::NoteUnknownKeys(
            WORLD_READER,
            root,
            "",
            "key",
            { "road_cost", "ford_cost", "tile_size", "kinds", "grid", "roads", "rivers", "fords", "places" },
            warnings
        );
        core::WorldMapSource source;
        // The grid needs the kinds, so they come before it.
        if (auto read = ReadSizes(root, source); !read) {
            return std::unexpected{ std::move(read.error()) };
        }
        if (auto read = ReadKinds(root, source, warnings); !read) {
            return std::unexpected{ std::move(read.error()) };
        }
        if (auto read = ReadGrid(root, yaml, source); !read) {
            return std::unexpected{ std::move(read.error()) };
        }
        if (auto read = ReadPolylines(root, "roads", source.roads, source.lines.roads); !read) {
            return std::unexpected{ std::move(read.error()) };
        }
        if (auto read = ReadPolylines(root, "rivers", source.rivers, source.lines.rivers); !read) {
            return std::unexpected{ std::move(read.error()) };
        }
        if (auto read = ReadFords(root, source); !read) {
            return std::unexpected{ std::move(read.error()) };
        }
        if (auto read = ReadPlaces(root, strings, source, warnings); !read) {
            return std::unexpected{ std::move(read.error()) };
        }
        return core::WorldMap::Build(std::move(source)).transform_error([](core::Error error) {
            return core::Error{ std::format("{}: {}", WORLD_READER, error.message) };
        });
    }
    catch (const YAML::Exception& error) {
        return std::unexpected{ internal::ErrorOf(WORLD_READER, error) };
    }
}

std::expected<std::shared_ptr<const core::WorldMap>, core::Error> LoadWorldMap(
    const std::filesystem::path& folder,
    const core::Strings& strings
) {
    std::vector<std::string> warnings;
    std::expected<core::WorldMap, core::Error> map =
        internal::ReadText(folder / "world.yaml").and_then([&](const std::string& text) {
            return ParseWorldMap(text, strings, &warnings);
        });
    for (const std::string& warning : warnings) {
        PN_LOG_WARNING("{}", warning);
    }
    if (!map) {
        return std::unexpected{ std::move(map.error()) };
    }
    return std::make_shared<const core::WorldMap>(std::move(*map));
}

} // namespace data
