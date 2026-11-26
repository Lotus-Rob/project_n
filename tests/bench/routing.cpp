// The routing benchmark (`routing_bench`): for each scale of the synthetic maps (tests/support/synthetic_map.hpp) it
// - builds every profile's route graph and place table (the table within the travel rules' reach bound, bounded and
//   unbounded ones apart) on the job system, best of 5, and prints their times, memory and counts;
// - times the searches of a trip model (each city to its 20 nearest places) and of random pairs in bands of
//   distance in tiles (0-15, 16-47, 48-79, 80-143, 144+), place to place and with a tile end, each alone: p99, mean, nodes
//   settled;
// - makes the segments the trips use and sizes them with the routes;
// - compares the routes' costs with core::FindRoute's cheapest on 20 maps and on two hostile ones (all meadow, wide
//   biomes with a danger reach of 16): the worst route over the cheapest and the worst pair of each band.
// It exits 1 over a budget (graph and place tables: 20 ms, 1 MB at 1x, 0.2 s, 10 MB at 10x, 1 s, 50 MB at 50x; one
// search's p99, in the trip model and place to place in the far band: 0.1, 0.5, 2 ms; segments and routes of the trip model: 2,
// 20, 100 MB) or when a route costs over 5% more than the cheapest. Run it optimised, on request and alone, never as a gate:
// `pixi run project_n test --mode release --target routing_bench [-- <flags>]`. Flags:
// --scales 1,10,50      the scales to run
// --sector N            a sector's side in tiles (default 32)
// --gate-spacing N      the longest stretch of a border one gate serves (default 4)
// --workers N           the job system's workers (default 4)
// --seed N              the maps' seed
// --chunk N             sectors a chunk builds
// --places N            places on each map instead of the scale's own
// --no-share            build every profile's sectors, none copied from another profile
// --fresh-scratch       every timed search gets a new workspace instead of one kept and warmed
// --single-segments     make the trip model's segments one edge at a time instead of in one call
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/jobs.hpp"
#include "core/log.hpp"
#include "core/math.hpp"
#include "core/place_table.hpp"
#include "core/route_graph.hpp"
#include "core/route_search.hpp"
#include "core/segments.hpp"
#include "support/synthetic_map.hpp"

namespace {

constexpr u32 BUILD_RUNS = 5;
constexpr f64 MEGABYTE = 1'000'000.0;
constexpr f64 NANOSECONDS_PER_MILLISECOND = 1'000'000.0;
constexpr std::string_view FLAG_LIST =
    "--scales, --sector, --gate-spacing, --workers, --seed, --chunk, --places, --no-share, --fresh-scratch, "
    "--single-segments";

/// What one scale of the map is held to, in the development machine's release build on 4 workers.
struct Budgets {
    /// Building the graphs and place tables, in milliseconds.
    f64 buildMs;
    /// The graphs and place tables in memory, in megabytes.
    f64 memoryMb;
    /// The 99th percentile of one place-to-place search, in milliseconds.
    f64 searchMs;
    /// The segments and routes of the trip model, in megabytes.
    f64 tripMb;
};

/// The budgets of the scales in test::SYNTHETIC_SCALES, in its order.
constexpr std::array<Budgets, test::SYNTHETIC_SCALES.size()> BUDGETS{ {
    { .buildMs = 20.0, .memoryMb = 1.0, .searchMs = 0.1, .tripMb = 2.0 },
    { .buildMs = 200.0, .memoryMb = 10.0, .searchMs = 0.5, .tripMb = 20.0 },
    { .buildMs = 1000.0, .memoryMb = 50.0, .searchMs = 2.0, .tripMb = 100.0 },
} };

/// How many nearest places each city's trips go to.
constexpr std::size_t TRIPS_PER_CITY = 20;
/// How many far pairs are searched in each band of distance in tiles.
constexpr u32 PAIRS_PER_BAND = 200;
/// How many maps the quality check compares routes on, each its own seed.
constexpr u32 QUALITY_MAPS = 20;
/// How many pairs it draws on each map in each band.
constexpr u32 QUALITY_PAIRS_PER_BAND = 50;
/// How much worse than the cheapest route a route may cost, in percent.
constexpr f64 QUALITY_LIMIT_PERCENT = 5.0;
/// The share of routes it counts as noticeably worse, in percent over the cheapest.
constexpr f64 QUALITY_NOTICE_PERCENT = 2.0;
/// How many ends a pair is redrawn for before its band is given up as empty.
constexpr u32 PAIR_ATTEMPTS = 20'000;
/// The side in tiles of the blocks a pair's distance is counted in, whatever sector size a run builds with, so every run
/// draws the same pairs.
constexpr u32 BAND_UNIT = 16;
/// The furthest distance in BAND_UNIT blocks each band holds: 0, 1-2, 3-4, 5-8 and further.
constexpr std::array<u32, 5> BAND_LAST{ { 0, 2, 4, 8, std::numeric_limits<u32>::max() } };
/// The names of the bands, in tiles of distance.
constexpr std::array<std::string_view, BAND_LAST.size()> BAND_NAMES{ { "0-15", "16-47", "48-79", "80-143", "144+" } };
/// The purposes of the pairs' draws: each end's pick of place or tile.
constexpr u32 PURPOSE_FIRST = 1;
constexpr u32 PURPOSE_SECOND = 2;

/// The parsed flags.
struct Options {
    /// The maps' scales, in times the test map's area.
    std::vector<u32> scales{ test::SYNTHETIC_SCALES.begin(), test::SYNTHETIC_SCALES.end() };
    /// The sector's side in tiles.
    u32 sector = core::SECTOR_SIZE;
    /// The longest stretch of a border one gate serves.
    u32 gateSpacing = core::GATE_SPACING;
    /// The job system's workers.
    u32 workers = 4;
    /// The maps' seed.
    u64 seed = 1;
    /// How many sectors a chunk builds.
    u32 chunk = core::SECTORS_PER_CHUNK;
    /// Whether profiles whose costs agree on a sector share its build.
    bool share = true;
    /// How many places the maps hold; 0 for each scale's own.
    u32 places = 0;
    /// Whether every search gets a workspace of its own instead of one kept across searches.
    bool freshScratch = false;
    /// Whether the trip model's segments are made one edge at a time instead of all at once.
    bool singleSegments = false;
};

/// One measured number against its budget.
struct Row {
    /// What was measured.
    std::string_view name;
    /// The number.
    f64 measured;
    /// The most it may be; 0 for no budget.
    f64 budget;
    /// What it is counted in.
    std::string_view unit;
    /// How many decimals it prints.
    u32 decimals = 3;
};

/// Logs each row as `<name>: <measured> <unit> (budget <budget>) [OVER]` and returns whether any is over its budget.
bool Print(std::span<const Row> rows) {
    bool over = false;
    for (const Row& row : rows) {
        const bool isOver = row.budget > 0.0 && row.measured > row.budget;
        over = over || isOver;
        const std::string unit = row.unit.empty() ? std::string{} : std::format(" {}", row.unit);
        if (row.budget > 0.0) {
            PN_LOG_INFO(
                "{}: {:.{}f}{} (budget {:.{}f}){}",
                row.name,
                row.measured,
                row.decimals,
                unit,
                row.budget,
                row.decimals,
                isOver ? " [OVER]" : ""
            );
        }
        else {
            PN_LOG_INFO("{}: {:.{}f}{}", row.name, row.measured, row.decimals, unit);
        }
    }
    return over;
}

/// The fastest of `runs` runs of `work`, in milliseconds; what `work` returns is kept until the clock has stopped.
f64 BestOf(u32 runs, const auto& work) {
    f64 best = std::numeric_limits<f64>::max();
    for (u32 run = 0; run < runs; ++run) {
        const auto start = std::chrono::steady_clock::now();
        [[maybe_unused]] const auto kept = work();
        best = std::min(best, std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    return best;
}

/// Reads `text` as a whole number, or nothing.
template<typename T>
std::optional<T> ParseNumber(std::string_view text) {
    T value{};
    const char* last = std::to_address(text.end());
    const auto [end, error] = std::from_chars(std::to_address(text.begin()), last, value);
    if (error != std::errc{} || end != last) {
        return std::nullopt;
    }
    return value;
}

/// The option `flag` sets to a whole number of at least 1, or null for a flag that sets none.
u32* NumberField(Options& options, std::string_view flag) {
    if (flag == "--sector") {
        return &options.sector;
    }
    if (flag == "--gate-spacing") {
        return &options.gateSpacing;
    }
    if (flag == "--workers") {
        return &options.workers;
    }
    if (flag == "--chunk") {
        return &options.chunk;
    }
    if (flag == "--places") {
        return &options.places;
    }
    return nullptr;
}

/// Turns on the switch `flag` names; false for a flag that names none.
bool SetSwitch(Options& options, std::string_view flag) {
    if (flag == "--no-share") {
        options.share = false;
    }
    else if (flag == "--fresh-scratch") {
        options.freshScratch = true;
    }
    else if (flag == "--single-segments") {
        options.singleSegments = true;
    }
    else {
        return false;
    }
    return true;
}

/// Sets `options.scales` from a comma-separated list of test::SYNTHETIC_SCALES; false when the list holds anything else.
bool SetScales(Options& options, std::string_view list) {
    std::vector<u32> scales;
    for (const auto part : std::views::split(list, ',')) {
        const auto scale = ParseNumber<u32>(std::string_view{ part.begin(), part.end() });
        if (!scale || !std::ranges::contains(test::SYNTHETIC_SCALES, *scale)) {
            return false;
        }
        scales.push_back(*scale);
    }
    options.scales = std::move(scales);
    return !options.scales.empty();
}

/// Sets the option `flag` to `value`; false when the value doesn't suit it.
bool SetOption(Options& options, std::string_view flag, std::string_view value) {
    if (flag == "--scales") {
        return SetScales(options, value);
    }
    if (flag == "--seed") {
        const auto seed = ParseNumber<u64>(value);
        options.seed = seed.value_or(options.seed);
        return seed.has_value();
    }
    u32* field = NumberField(options, flag);
    const auto number = ParseNumber<u32>(value);
    if (field == nullptr || !number || *number < 1) {
        return false;
    }
    *field = *number;
    return true;
}

/// The flags in `args`, or nothing after logging what was wrong with them.
std::optional<Options> Parse(std::span<const std::string_view> args) {
    Options options;
    for (auto arg = args.begin(); arg != args.end(); ++arg) {
        if (SetSwitch(options, *arg)) {
            continue;
        }
        if (*arg != "--scales" && *arg != "--seed" && NumberField(options, *arg) == nullptr) {
            PN_LOG_ERROR("unknown flag {}; the flags are {}", *arg, FLAG_LIST);
            return std::nullopt;
        }
        if (std::next(arg) == args.end()) {
            PN_LOG_ERROR("{} needs a value; the flags are {}", *arg, FLAG_LIST);
            return std::nullopt;
        }
        const std::string_view flag = *arg;
        const std::string_view value = *++arg;
        if (!SetOption(options, flag, value)) {
            PN_LOG_ERROR("{} doesn't take '{}': see the flags, {}", flag, value, FLAG_LIST);
            return std::nullopt;
        }
    }
    return options;
}

/// Rows whose names are made as they are added.
class Table {
public:
    /// Adds a row.
    void Add(std::string name, f64 measured, f64 budget, std::string_view unit, u32 decimals = 3) {
        m_names.push_back(std::move(name));
        m_rows.push_back({ .name = {}, .measured = measured, .budget = budget, .unit = unit, .decimals = decimals });
    }
    /// Adds a line of text in the rows' place.
    void AddNote(std::string text) {
        Add(std::move(text), 0.0, 0.0, "");
        m_notes.push_back(m_rows.size() - 1);
    }
    /// Logs the rows and notes in the order added; whether any row is over its budget.
    [[nodiscard]] bool Print() {
        bool over = false;
        for (std::size_t i = 0; i < m_rows.size(); ++i) {
            m_rows[i].name = m_names[i]; // views taken once the names no longer move
            if (std::ranges::contains(m_notes, i)) {
                PN_LOG_INFO("{}", m_names[i]);
            }
            else {
                over = ::Print(std::span<const Row>{ &m_rows[i], 1 }) || over;
            }
        }
        return over;
    }

private:
    /// The rows' names.
    std::vector<std::string> m_names;
    /// The rows, their names set when printed.
    std::vector<Row> m_rows;
    /// The positions of the entries that are notes, not rows.
    std::vector<std::size_t> m_notes;
};

/// The budgets of `scale`, one of test::SYNTHETIC_SCALES.
const Budgets& BudgetsOf(u32 scale) {
    return BUDGETS.at(
        static_cast<std::size_t>(std::ranges::find(test::SYNTHETIC_SCALES, scale) - test::SYNTHETIC_SCALES.begin())
    );
}

/// A map with its routing rules and every profile's graph.
struct World {
    /// The map.
    const core::WorldMap* mapPointer = nullptr;
    /// The rules the graphs follow.
    core::RoutingRules rules;
    /// The graphs.
    const core::RouteGraphs* graphsPointer = nullptr;

    /// The map.
    [[nodiscard]] const core::WorldMap& Map() const {
        return *mapPointer;
    }
    /// The graphs.
    [[nodiscard]] const core::RouteGraphs& Graphs() const {
        return *graphsPointer;
    }
};

/// How many nanoseconds `work` took.
u64 TimeNs(const auto& work) {
    const auto start = std::chrono::steady_clock::now();
    work();
    return static_cast<u64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count()
    );
}

/// The durations of searches, in nanoseconds.
class Samples {
public:
    /// Adds one.
    void Add(u64 nanoseconds) {
        m_nanoseconds.push_back(nanoseconds);
        m_total += nanoseconds;
    }
    /// How many there are.
    [[nodiscard]] std::size_t Size() const {
        return m_nanoseconds.size();
    }
    /// All of them added up, in nanoseconds.
    [[nodiscard]] f64 TotalNs() const {
        return static_cast<f64>(m_total);
    }
    /// Their mean, in milliseconds.
    [[nodiscard]] f64 MeanMs() const {
        return Size() == 0 ? 0.0 : TotalNs() / static_cast<f64>(Size()) / NANOSECONDS_PER_MILLISECOND;
    }
    /// The 99th percentile (the shortest that at least 99% do not exceed), in milliseconds.
    [[nodiscard]] f64 P99Ms() {
        if (Size() == 0) {
            return 0.0;
        }
        std::ranges::sort(m_nanoseconds);
        return static_cast<f64>(m_nanoseconds.at((((Size() * 99) + 99) / 100) - 1)) / NANOSECONDS_PER_MILLISECOND;
    }

private:
    /// The durations.
    std::vector<u64> m_nanoseconds;
    /// Their sum.
    u64 m_total = 0;
};

/// What a run of searches took and did.
struct SearchStats {
    /// Each search's time.
    Samples times;
    /// The nodes the searches settled, in all.
    u64 popped = 0;
    /// The links they relaxed, in all.
    u64 relaxed = 0;
    /// The most nodes one settled.
    u32 mostPopped = 0;
    /// The most links one relaxed.
    u32 mostRelaxed = 0;

    /// Adds a search that took `nanoseconds` and did `counts`.
    void Record(u64 nanoseconds, core::SearchCounts counts) {
        times.Add(nanoseconds);
        popped += counts.popped;
        relaxed += counts.relaxed;
        mostPopped = std::max(mostPopped, counts.popped);
        mostRelaxed = std::max(mostRelaxed, counts.relaxed);
    }

    /// Adds the rows of the searches to `table`, named from `prefix`; the 99th percentile is held to `budgetMs` when it is
    /// above 0. `nodes` is the graph's node count, to give the settled nodes as a share of it; `full` adds the counts too.
    void AddRows(Table& table, std::string_view prefix, f64 budgetMs, u32 nodes, bool full) {
        if (times.Size() == 0) {
            table.AddNote(std::format("{}: no pairs, budget not checked", prefix));
            return;
        }
        const f64 searches = static_cast<f64>(times.Size());
        const f64 meanPopped = static_cast<f64>(popped) / searches;
        table.Add(std::format("{}, searches", prefix), searches, 0.0, "", 0);
        table.Add(std::format("{}, p99", prefix), times.P99Ms(), budgetMs, "ms", 4);
        table.Add(std::format("{}, mean", prefix), times.MeanMs(), 0.0, "ms", 4);
        table.Add(
            std::format("{}, nodes settled, share of the graph's", prefix),
            meanPopped * 100.0 / static_cast<f64>(nodes),
            0.0,
            "%",
            2
        );
        if (!full) {
            return;
        }
        table.Add(std::format("{}, nodes settled, mean", prefix), meanPopped, 0.0, "", 0);
        table.Add(std::format("{}, nodes settled, most", prefix), static_cast<f64>(mostPopped), 0.0, "", 0);
        table.Add(std::format("{}, links relaxed, mean", prefix), static_cast<f64>(relaxed) / searches, 0.0, "", 0);
        table.Add(std::format("{}, links relaxed, most", prefix), static_cast<f64>(mostRelaxed), 0.0, "", 0);
        table.Add(
            std::format("{}, a settled node", prefix),
            times.TotalNs() / static_cast<f64>(std::max<u64>(popped, 1)),
            0.0,
            "ns",
            1
        );
    }
};

/// The route from `from` to `target`, timed alone and recorded in `stats`. `shared` is the workspace kept across
/// searches, or null for one made for this search alone, which is timed with it.
std::optional<core::GraphRoute> Timed(
    const World& world,
    core::ProfileIndex profile,
    u32 from,
    u32 target,
    core::RouteScratch* shared,
    SearchStats& stats
) {
    std::optional<core::GraphRoute> route;
    core::SearchCounts counts;
    const u64 nanoseconds = TimeNs([&] {
        std::optional<core::RouteScratch> fresh;
        if (shared == nullptr) {
            fresh.emplace();
        }
        core::RouteScratch& used = shared == nullptr ? *fresh : *shared;
        route = core::FindGraphRoute(world.Map(), world.Graphs().Of(profile), from, target, used);
        counts = used.Counts();
    });
    stats.Record(nanoseconds, counts);
    return route;
}

/// The profile's place tables' parts of one scale: each profile's table and the time it took to build.
struct Tables {
    /// Each profile's table, by profile.
    std::vector<core::PlaceTable> tables;
    /// The bounds the tables were built within, by profile.
    std::vector<u32> bounds;
    /// The best time each took, in milliseconds, by profile.
    std::vector<f64> buildMs;
};

/// Builds each profile's place table within the reach bound of the map's travel rules, timed on `jobs`; the last timed
/// build is the one kept.
Tables BuildTables(const core::WorldMap& map, const core::RouteGraphs& graphs, core::Jobs& jobs) {
    Tables built;
    for (std::size_t index = 0; index < map.Profiles().size(); ++index) {
        const auto profile = static_cast<core::ProfileIndex>(index);
        const u32 bound = core::ReachBound(map.Travel(), profile);
        const core::RouteGraph& graph = graphs.Of(profile);
        built.bounds.push_back(bound);
        f64 best = std::numeric_limits<f64>::max();
        for (u32 run = 0; run < BUILD_RUNS; ++run) {
            const auto start = std::chrono::steady_clock::now();
            core::PlaceTable table = core::PlaceTable::Build(map, graph, bound, &jobs);
            best = std::min(best, std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count());
            if (run + 1 == BUILD_RUNS) {
                built.tables.push_back(std::move(table));
            }
        }
        built.buildMs.push_back(best);
    }
    return built;
}

/// How many bytes `tables` hold, those built within a bound (`bounded`) or those built without one.
f64 TableMegabytes(const Tables& tables, bool bounded) {
    f64 bytes = 0.0;
    for (std::size_t index = 0; index < tables.tables.size(); ++index) {
        bytes += (tables.bounds[index] != core::UNREACHABLE) == bounded ? static_cast<f64>(tables.tables[index].Bytes()) : 0.0;
    }
    return bytes / MEGABYTE;
}

/// How long the tables built within a bound (`bounded`) or without one took, in milliseconds.
f64 TableMs(const Tables& tables, bool bounded) {
    f64 total = 0.0;
    for (std::size_t index = 0; index < tables.tables.size(); ++index) {
        total += (tables.bounds[index] != core::UNREACHABLE) == bounded ? tables.buildMs[index] : 0.0;
    }
    return total;
}

/// How many edges of `graph` are steps over a border.
u64 CrossingEdges(const core::RouteGraph& graph) {
    u64 crossings = 0;
    for (u32 edge = 0; edge < graph.Edges().size(); ++edge) {
        crossings += graph.IsCrossing(edge) ? 1 : 0;
    }
    return crossings;
}

/// The label of a table's bound.
std::string BoundLabel(u32 bound) {
    return bound == core::UNREACHABLE ? std::string{ "unbounded" } : std::format("bounded at {}", bound);
}

/// Logs the builds' times, their memory and the graphs' counts; returns whether a build or memory row is over.
bool ReportBuild(
    const Options& options,
    const Budgets& budgets,
    const World& world,
    const Tables& tables,
    f64 graphsMs,
    f64 serialMs
) {
    const std::span<const core::MovementProfile> profiles = world.Map().Profiles();
    const f64 sectors = static_cast<f64>(core::SectorColumns(world.Map(), world.rules)) *
                        static_cast<f64>(core::SectorRows(world.Map(), world.rules));
    Table build;
    build.Add(std::format("graphs build, {} workers", options.workers), graphsMs, 0.0, "ms");
    build.Add("graphs build, no job system", serialMs, 0.0, "ms");
    for (std::size_t profile = 0; profile < profiles.size(); ++profile) {
        build.Add(
            std::format("place table build, {} ({})", profiles[profile].key, BoundLabel(tables.bounds[profile])),
            tables.buildMs[profile],
            0.0,
            "ms"
        );
    }
    build.Add("place tables build, bounded ones", TableMs(tables, true), 0.0, "ms");
    build.Add("place tables build, unbounded ones", TableMs(tables, false), 0.0, "ms");
    build.Add(
        std::format("build, graphs and place tables, {} workers", options.workers),
        graphsMs + TableMs(tables, true) + TableMs(tables, false),
        budgets.buildMs,
        "ms"
    );
    bool over = build.Print();

    Table memory;
    const f64 graphMb = static_cast<f64>(world.Graphs().Bytes()) / MEGABYTE;
    memory.Add(
        "memory, graphs and place tables",
        graphMb + TableMegabytes(tables, true) + TableMegabytes(tables, false),
        budgets.memoryMb,
        "MB"
    );
    memory.Add("memory, all graphs", graphMb, 0.0, "MB");
    for (std::size_t profile = 0; profile < profiles.size(); ++profile) {
        const auto index = static_cast<core::ProfileIndex>(profile);
        memory.Add(
            std::format("memory, {} graph", profiles[profile].key),
            static_cast<f64>(world.Graphs().Of(index).Bytes()) / MEGABYTE,
            0.0,
            "MB"
        );
        memory.Add(
            std::format("memory, {} place table", profiles[profile].key),
            static_cast<f64>(tables.tables[profile].Bytes()) / MEGABYTE,
            0.0,
            "MB"
        );
    }
    over = memory.Print() || over;

    Table counts;
    counts.Add("places", static_cast<f64>(world.Map().Places().size()), 0.0, "", 0);
    counts.Add("sectors", sectors, 0.0, "", 0);
    counts.Add("sectors x profiles", sectors * static_cast<f64>(profiles.size()), 0.0, "", 0);
    counts.Add("sectors built, the rest copied", static_cast<f64>(world.Graphs().SectorsBuilt()), 0.0, "", 0);
    for (std::size_t profile = 0; profile < profiles.size(); ++profile) {
        const core::RouteGraph& graph = world.Graphs().Of(static_cast<core::ProfileIndex>(profile));
        const std::string& key = profiles[profile].key;
        counts.Add(std::format("{} nodes", key), static_cast<f64>(graph.Nodes()), 0.0, "", 0);
        counts.Add(std::format("{} edges", key), static_cast<f64>(graph.Edges().size()), 0.0, "", 0);
        counts.Add(std::format("{} gates a sector", key), static_cast<f64>(CrossingEdges(graph)) / sectors, 0.0, "", 2);
    }
    static_cast<void>(counts.Print());
    return over;
}

/// The trips of the places' nearest `TRIPS_PER_CITY`: for each city with a node in `graph`, its tile and each of the
/// nearest places' (by `table`'s cost, the lower place on a tie), as pairs of tiles.
std::vector<std::pair<u32, u32>> TripsOf(
    const core::WorldMap& map,
    const core::RouteGraph& graph,
    const core::PlaceTable& table
) {
    std::vector<std::pair<u32, u32>> trips;
    std::vector<std::pair<u32, u16>> near;
    const std::span<const core::Place> places = map.Places();
    for (std::size_t city = 0; city < places.size(); ++city) {
        if (places[city].kind != core::PlaceKind::City || graph.PlaceNode(static_cast<u16>(city)) == core::NO_NODE) {
            continue;
        }
        near.clear();
        for (std::size_t place = 0; place < places.size(); ++place) {
            const u32 cost = table.Cost(static_cast<u16>(city), static_cast<u16>(place));
            if (place != city && cost != core::UNREACHABLE) {
                near.emplace_back(cost, static_cast<u16>(place));
            }
        }
        const std::size_t taken = std::min(near.size(), TRIPS_PER_CITY);
        std::ranges::partial_sort(near, near.begin() + static_cast<std::ptrdiff_t>(taken));
        for (std::size_t i = 0; i < taken; ++i) {
            trips.emplace_back(map.TileIndex(places[city].tile), map.TileIndex(places[near[i].second].tile));
        }
    }
    return trips;
}

/// The edges of `routes`' steps, in order.
std::vector<u32> EdgesOf(const std::vector<core::GraphRoute>& routes) {
    std::vector<u32> edges;
    for (const core::GraphRoute& route : routes) {
        std::ranges::transform(route.steps, std::back_inserter(edges), &core::RouteStep::edge);
    }
    return edges;
}

/// Makes the segments of `edges` in `store`, all in one `Add` or one `Add` an edge, and returns the time it took in
/// milliseconds.
f64 AddSegments(core::SegmentStore& store, const std::vector<u32>& edges, bool singly) {
    const u64 nanoseconds = TimeNs([&] {
        if (!singly) {
            store.Add(edges);
            return;
        }
        for (const u32& edge : edges) {
            store.Add(std::span<const u32>{ &edge, 1 });
        }
    });
    return static_cast<f64>(nanoseconds) / NANOSECONDS_PER_MILLISECOND;
}

/// The tiles of the segments `store` made for `edges`, each edge once.
u64 SegmentTiles(const core::SegmentStore& store, std::vector<u32> edges) {
    std::ranges::sort(edges);
    const auto unique = std::ranges::unique(edges);
    u64 tiles = 0;
    for (const u32 edge : std::ranges::subrange(edges.begin(), unique.begin())) {
        tiles += store.Find(edge)->tiles.size();
    }
    return tiles;
}

/// Runs the trip model on every profile, logging the searches and then the segments they need; returns whether a search
/// or the model's memory is over its budget.
bool MeasureTrips(
    const Options& options,
    const Budgets& budgets,
    const World& world,
    const Tables& tables,
    core::RouteScratch* shared
) {
    const std::span<const core::MovementProfile> profiles = world.Map().Profiles();
    Table searches;
    Table segments;
    f64 modelBytes = 0.0;
    for (std::size_t index = 0; index < profiles.size(); ++index) {
        const auto profile = static_cast<core::ProfileIndex>(index);
        const core::RouteGraph& graph = world.Graphs().Of(profile);
        const std::string& key = profiles[index].key;
        SearchStats stats;
        std::vector<core::GraphRoute> routes;
        for (const auto& [from, target] : TripsOf(world.Map(), graph, tables.tables[index])) {
            std::optional<core::GraphRoute> route = Timed(world, profile, from, target, shared, stats);
            if (route) {
                routes.push_back(std::move(*route));
            }
        }
        stats.AddRows(searches, std::format("trips, {}", key), budgets.searchMs, graph.Nodes(), true);

        core::SegmentStore store{ world.Map(), graph };
        const std::vector<u32> edges = EdgesOf(routes);
        const f64 addMs = AddSegments(store, edges, options.singleSegments);
        const std::size_t routeBytes =
            std::accumulate(routes.begin(), routes.end(), std::size_t{ 0 }, [](std::size_t sum, const core::GraphRoute& route) {
                return sum + route.Bytes();
            });
        const f64 tiles = static_cast<f64>(std::max<u64>(SegmentTiles(store, edges), 1));
        modelBytes += static_cast<f64>(store.Bytes() + routeBytes);
        segments.Add(std::format("segments, {}, adds", key), addMs, 0.0, "ms");
        segments.Add(std::format("segments, {}, made", key), static_cast<f64>(store.Made()), 0.0, "", 0);
        segments.Add(
            std::format("segments, {}, share of the graph's edges", key),
            static_cast<f64>(store.Made()) * 100.0 / static_cast<f64>(graph.Edges().size()),
            0.0,
            "%",
            2
        );
        segments.Add(std::format("segments, {}, bytes a tile", key), static_cast<f64>(store.Bytes()) / tiles, 0.0, "", 1);
        segments.Add(
            std::format("segments, {}, walk tables' share", key),
            static_cast<f64>(store.WalkBytes()) * 100.0 / static_cast<f64>(std::max<std::size_t>(store.Bytes(), 1)),
            0.0,
            "%",
            1
        );
        segments.Add(std::format("routes, {}, bytes", key), static_cast<f64>(routeBytes) / MEGABYTE, 0.0, "MB");
    }
    segments.Add("segments and routes of the trip model", modelBytes / MEGABYTE, budgets.tripMb, "MB");
    const bool over = searches.Print();
    return segments.Print() || over;
}

/// The distance between two tiles in blocks of BAND_UNIT tiles: the larger of the distances in columns and rows.
u32 SectorDistance(const core::WorldMap& map, u32 first, u32 second) {
    const auto sectorOf = [&](u32 tile) {
        return std::pair{ (tile % map.Columns()) / BAND_UNIT, (tile / map.Columns()) / BAND_UNIT };
    };
    const auto [firstColumn, firstRow] = sectorOf(first);
    const auto [secondColumn, secondRow] = sectorOf(second);
    const u32 across = std::max(firstColumn, secondColumn) - std::min(firstColumn, secondColumn);
    const u32 down = std::max(firstRow, secondRow) - std::min(firstRow, secondRow);
    return std::max(across, down);
}

/// The band `distance` blocks of BAND_UNIT tiles falls in.
std::size_t BandOf(u32 distance) {
    return static_cast<std::size_t>(
        std::ranges::find_if(BAND_LAST, [&](u32 last) { return distance <= last; }) - BAND_LAST.begin()
    );
}

/// What a pair's ends are drawn from.
struct EndKinds {
    /// Whether the first end is a place; a tile `profile` can enter otherwise.
    bool firstPlace;
    /// Whether the second end is a place.
    bool secondPlace;
    /// Whether the places are cities only.
    bool citiesOnly;
};

/// The tiles of the places `profile` can enter, cities only or all.
std::vector<u32> PlaceTiles(const core::WorldMap& map, core::ProfileIndex profile, bool citiesOnly) {
    std::vector<u32> tiles;
    for (const core::Place& place : map.Places()) {
        const u32 tile = map.TileIndex(place.tile);
        if ((!citiesOnly || place.kind == core::PlaceKind::City) && map.CostAt(tile, profile) != core::IMPASSABLE_COST) {
            tiles.push_back(tile);
        }
    }
    return tiles;
}

/// Draws pairs of tiles on one map, redrawing until a pair's ends lie in a band of distance in tiles.
class PairDrawer {
public:
    /// A drawer of pairs on `world` for `profile`, from `seed`.
    PairDrawer(const World& world, core::ProfileIndex profile, u64 seed)
      : m_world(&world)
      , m_profile(profile)
      , m_seed(seed)
      , m_cityTiles(PlaceTiles(world.Map(), profile, true))
      , m_placeTiles(PlaceTiles(world.Map(), profile, false)) {
    }

    /// Pair number `index` of `band` with ends of `kinds`, one not yet in `drawn` (the pair either way round), which it
    /// joins; none when no new pair in the band turns up in PAIR_ATTEMPTS draws.
    [[nodiscard]] std::optional<std::pair<u32, u32>> Draw(
        std::size_t band,
        u32 index,
        EndKinds kinds,
        std::unordered_set<u64>& drawn
    ) const {
        const std::vector<u32>& places = kinds.citiesOnly ? m_cityTiles : m_placeTiles;
        if (places.empty()) {
            return std::nullopt;
        }
        for (u32 attempt = 0; attempt < PAIR_ATTEMPTS; ++attempt) {
            const u32 first = End(places, kinds.firstPlace, band, index, PURPOSE_FIRST, attempt);
            const u32 second = End(places, kinds.secondPlace, band, index, PURPOSE_SECOND, attempt);
            const u64 key = (u64{ std::min(first, second) } << 32U) | std::max(first, second);
            if (first != second && BandOf(SectorDistance(m_world->Map(), first, second)) == band && drawn.insert(key).second) {
                return std::pair{ first, second };
            }
        }
        return std::nullopt;
    }

private:
    /// The end drawn for `purpose` of pair `index` of `band` on `attempt`: one of `places`, or a tile the profile can enter.
    [[nodiscard]] u32 End(
        const std::vector<u32>& places,
        bool place,
        std::size_t band,
        u32 index,
        u32 purpose,
        u32 attempt
    ) const {
        // The tile draws walk on from the attempt's number, so a closed tile redraws without disturbing the next attempt.
        const u64 stable = (u64{ index } << 32U) | attempt;
        if (place) {
            return places.at(core::ToBelow(core::Random(m_seed, band, stable, purpose, 0), static_cast<u32>(places.size())));
        }
        const auto tiles = static_cast<u32>(u64{ m_world->Map().Columns() } * m_world->Map().Rows());
        for (u32 draw = 1;; ++draw) {
            const u32 tile = core::ToBelow(core::Random(m_seed, band, stable, purpose, draw), tiles);
            if (m_world->Map().CostAt(tile, m_profile) != core::IMPASSABLE_COST) {
                return tile;
            }
        }
    }

    /// The world the pairs are on.
    const World* m_world;
    /// The profile whose tiles they are.
    core::ProfileIndex m_profile;
    /// The seed of the draws.
    u64 m_seed;
    /// The tiles of the cities the profile can enter.
    std::vector<u32> m_cityTiles;
    /// The tiles of every place the profile can enter.
    std::vector<u32> m_placeTiles;
};

/// Searches up to PAIRS_PER_BAND pairs of `band` drawn by `drawer`, each alone, from a city to a city or to a tile.
SearchStats SearchBand(
    const World& world,
    core::ProfileIndex profile,
    const PairDrawer& drawer,
    std::size_t band,
    bool tileEnd,
    core::RouteScratch* shared
) {
    SearchStats stats;
    std::unordered_set<u64> drawn;
    for (u32 pair = 0; pair < PAIRS_PER_BAND; ++pair) {
        const auto ends = drawer.Draw(band, pair, { .firstPlace = true, .secondPlace = !tileEnd, .citiesOnly = true }, drawn);
        if (!ends) {
            break;
        }
        static_cast<void>(Timed(world, profile, ends->first, ends->second, shared, stats));
    }
    return stats;
}

/// Searches random pairs in each band of distance in tiles, each alone, for every profile: city to city, then city to a
/// tile; returns whether the far band's place-to-place p99 is over its budget.
bool MeasureFarPairs(const Options& options, const Budgets& budgets, const World& world, core::RouteScratch* shared) {
    const std::span<const core::MovementProfile> profiles = world.Map().Profiles();
    Table table;
    for (std::size_t index = 0; index < profiles.size(); ++index) {
        const auto profile = static_cast<core::ProfileIndex>(index);
        const PairDrawer drawer{ world, profile, options.seed };
        for (const bool tileEnd : { false, true }) {
            for (std::size_t band = 0; band < BAND_LAST.size(); ++band) {
                const bool far = band + 1 == BAND_LAST.size();
                const f64 budget = far && !tileEnd ? budgets.searchMs : 0.0;
                SearchBand(world, profile, drawer, band, tileEnd, shared)
                    .AddRows(
                        table,
                        std::format(
                            "far pairs, {}, {}, {} tiles",
                            profiles[index].key,
                            tileEnd ? "city to tile" : "city to city",
                            BAND_NAMES.at(band)
                        ),
                        budget,
                        world.Graphs().Of(profile).Nodes(),
                        false
                    );
            }
        }
    }
    return table.Print();
}

/// What walking `tiles` costs by `profile`, step by step.
u32 WalkCost(const core::WorldMap& map, core::ProfileIndex profile, const std::vector<u32>& tiles) {
    u32 total = 0;
    for (std::size_t step = 1; step < tiles.size(); ++step) {
        total += map.StepCost(tiles[step - 1], tiles[step], profile);
    }
    return total;
}

/// How the routes of one profile in one band compare with the cheapest.
struct QualityBand {
    /// The pairs compared.
    u64 compared = 0;
    /// How many cost more than QUALITY_NOTICE_PERCENT over the cheapest.
    u64 noticed = 0;
    /// The worst route's cost over the cheapest, in percent.
    f64 worstPercent = 0.0;
    /// The worst route's pair, its costs and the map's seed, written out.
    std::string worst;
    /// How many pairs the cheapest search and the graph's disagreed on whether a route exists.
    u64 unmatched = 0;
};

/// One pair to compare: its ends and band, and what each search found.
struct Comparison {
    /// The tile the route starts on.
    u32 from = 0;
    /// The tile it ends on.
    u32 target = 0;
    /// The pair's band.
    std::size_t band = 0;
    /// The cost of the graph's route; UNREACHABLE for none.
    u32 graphCost = core::UNREACHABLE;
    /// The cost of the cheapest route; UNREACHABLE for none.
    u32 cheapestCost = core::UNREACHABLE;
};

/// `tile` as a place's key and its column and row, or just its column and row.
std::string Describe(const core::WorldMap& map, u32 tile) {
    const u16 place = map.PlaceAt(tile);
    const std::string where = std::format("({}, {})", tile % map.Columns(), tile / map.Columns());
    return place == core::NO_PLACE ? std::format("tile {}", where) : std::format("place {} {}", map.Places()[place].key, where);
}

/// Adds the comparisons `one` of the map of `seed` to `into`.
void Tally(const core::WorldMap& map, u64 seed, const Comparison& one, QualityBand& into) {
    const bool found = one.graphCost != core::UNREACHABLE;
    if (found != (one.cheapestCost != core::UNREACHABLE)) {
        ++into.unmatched;
    }
    if (!found || one.cheapestCost == core::UNREACHABLE || one.cheapestCost == 0) {
        return;
    }
    const f64 percent = ((static_cast<f64>(one.graphCost) / static_cast<f64>(one.cheapestCost)) - 1.0) * 100.0;
    ++into.compared;
    into.noticed += percent > QUALITY_NOTICE_PERCENT ? 1 : 0;
    if (percent > into.worstPercent) {
        into.worstPercent = percent;
        into.worst = std::format(
            "seed {}, {} to {}, cheapest {}, {} more",
            seed,
            Describe(map, one.from),
            Describe(map, one.target),
            one.cheapestCost,
            i64{ one.graphCost } - i64{ one.cheapestCost }
        );
    }
}

/// Compares the graph's routes with the cheapest on `world` for `profile`, QUALITY_PAIRS_PER_BAND pairs drawn in each band
/// from `seed`, each routed both ways, adding what it finds to `into`.
void CompareProfile(
    const World& world,
    core::ProfileIndex profile,
    u64 seed,
    core::Jobs& jobs,
    std::array<QualityBand, BAND_LAST.size()>& into
) {
    const PairDrawer drawer{ world, profile, seed };
    std::unordered_set<u64> drawn;
    std::vector<Comparison> comparisons;
    for (std::size_t band = 0; band < BAND_LAST.size(); ++band) {
        for (u32 pair = 0; pair < QUALITY_PAIRS_PER_BAND; ++pair) {
            // Every band gets all four mixes of ends: two places, a place and a tile, a tile and a place, two tiles.
            const auto ends = drawer.Draw(
                band, pair, { .firstPlace = (pair % 2) == 0, .secondPlace = ((pair / 2) % 2) == 0, .citiesOnly = false }, drawn
            );
            // One mix running out of pairs in a band leaves the other mixes still to draw from.
            if (!ends) {
                continue;
            }
            comparisons.push_back({ .from = ends->first, .target = ends->second, .band = band });
            comparisons.push_back({ .from = ends->second, .target = ends->first, .band = band });
        }
    }
    const u32 stripes = jobs.Workers() + 1;
    jobs.ParallelFor(stripes, [&](u32 stripe) {
        core::RouteScratch scratch;
        for (std::size_t i = stripe; i < comparisons.size(); i += stripes) {
            Comparison& one = comparisons[i];
            const std::vector<u32> cheapest = core::FindRoute(world.Map(), profile, one.from, one.target);
            if (!cheapest.empty()) {
                one.cheapestCost = WalkCost(world.Map(), profile, cheapest);
            }
            const std::optional<core::GraphRoute> route =
                core::FindGraphRoute(world.Map(), world.Graphs().Of(profile), one.from, one.target, scratch);
            if (route) {
                one.graphCost = route->cost;
            }
        }
    });
    for (const Comparison& one : comparisons) {
        Tally(world.Map(), seed, one, into.at(one.band));
    }
}

/// The bands of every profile of one group of maps.
using Quality = std::vector<std::array<QualityBand, BAND_LAST.size()>>;

/// Logs `quality`, the comparisons made on the maps called `group`, one row a profile and band with its worst pair;
/// returns whether any route is over QUALITY_LIMIT_PERCENT or any pair disagrees on having a route.
bool ReportQuality(std::string_view group, std::span<const core::MovementProfile> profiles, const Quality& quality) {
    Table table;
    bool unmatched = false;
    for (std::size_t profile = 0; profile < profiles.size(); ++profile) {
        for (std::size_t band = 0; band < BAND_LAST.size(); ++band) {
            const QualityBand& result = quality.at(profile).at(band);
            unmatched = unmatched || result.unmatched > 0;
            if (result.compared + result.unmatched == 0) {
                table.AddNote(
                    std::format(
                        "quality, {}, {}, {} tiles: no pairs, budget not checked",
                        group,
                        profiles[profile].key,
                        BAND_NAMES.at(band)
                    )
                );
                continue;
            }
            table.Add(
                std::format(
                    "quality, {}, {}, {} tiles ({} pairs, {:.2f}% over {:.0f}%, {} unmatched; worst: {}), worst over the "
                    "cheapest",
                    group,
                    profiles[profile].key,
                    BAND_NAMES.at(band),
                    result.compared,
                    static_cast<f64>(result.noticed) * 100.0 / static_cast<f64>(std::max<u64>(result.compared, 1)),
                    QUALITY_NOTICE_PERCENT,
                    result.unmatched,
                    result.worst.empty() ? std::string{ "none over" } : result.worst
                ),
                result.worstPercent,
                QUALITY_LIMIT_PERCENT,
                "%",
                2
            );
        }
    }
    return table.Print() || unmatched;
}

/// Compares the routes on `world`, a map built from `seed`, adding to `quality`.
void CompareMap(const World& world, u64 seed, core::Jobs& jobs, Quality& quality) {
    for (std::size_t profile = 0; profile < world.Map().Profiles().size(); ++profile) {
        CompareProfile(world, static_cast<core::ProfileIndex>(profile), seed, jobs, quality.at(profile));
    }
}

/// Builds the graphs of `map`, built from `seed`, as `options` say, and compares its routes, adding to `quality`.
void CompareNewMap(const core::WorldMap& map, const Options& options, u64 seed, core::Jobs& jobs, Quality& quality) {
    const core::RoutingRules rules{ .sectorSize = options.sector, .gateSpacing = options.gateSpacing };
    const core::RouteGraphs graphs =
        core::RouteGraphs::Build(map, rules, { .jobs = &jobs, .sectorsPerChunk = options.chunk, .share = options.share });
    CompareMap({ .mapPointer = &map, .rules = rules, .graphsPointer = &graphs }, seed, jobs, quality);
}

/// Compares the graph's routes with the cheapest on QUALITY_MAPS maps of `scale`, the first being `first`, and on the two
/// hostile ones; returns whether any is over.
bool MeasureQuality(const Options& options, u32 scale, core::Jobs& jobs, const World& first) {
    const std::span<const core::MovementProfile> profiles = first.Map().Profiles();
    bool over = false;
    Quality seeded(profiles.size());
    CompareMap(first, options.seed, jobs, seeded);
    for (u32 offset = 1; offset < QUALITY_MAPS; ++offset) {
        const u64 seed = options.seed + offset;
        CompareNewMap(test::SyntheticMap(scale, seed, { .places = options.places }), options, seed, jobs, seeded);
    }
    over = ReportQuality(std::format("{} maps", QUALITY_MAPS), profiles, seeded) || over;
    const std::array<std::pair<std::string_view, test::SyntheticTerrain>, 2> hostile{ {
        { "all meadow, no roads", test::SyntheticTerrain::Meadow },
        { "wide biomes, danger reach 16", test::SyntheticTerrain::Coarse },
    } };
    for (const auto& [name, terrain] : hostile) {
        Quality one(profiles.size());
        CompareNewMap(
            test::SyntheticMap(scale, options.seed, { .places = options.places, .terrain = terrain }),
            options,
            options.seed,
            jobs,
            one
        );
        over = ReportQuality(name, profiles, one) || over;
    }
    return over;
}

/// Runs one search per graph, between the first and last city and from the first city to the last tile, that is not
/// timed, so `scratch` is sized to the graphs before any timed search uses it.
void Warm(const World& world, core::RouteScratch& scratch) {
    SearchStats unused;
    const core::WorldMap& map = world.Map();
    for (std::size_t index = 0; index < map.Profiles().size(); ++index) {
        const auto profile = static_cast<core::ProfileIndex>(index);
        const std::vector<u32> cities = PlaceTiles(map, profile, true);
        u32 last = static_cast<u32>(u64{ map.Columns() } * map.Rows()) - 1;
        while (last > 0 && map.CostAt(last, profile) == core::IMPASSABLE_COST) {
            --last;
        }
        if (cities.size() >= 2) {
            static_cast<void>(Timed(world, profile, cities.front(), cities.back(), &scratch, unused));
        }
        if (!cities.empty()) {
            static_cast<void>(Timed(world, profile, cities.front(), last, &scratch, unused));
        }
    }
}

/// Builds the graphs and place tables of one scale, logs every table; returns whether any row is over its budget.
bool Measure(const Options& options, u32 scale) {
    const core::WorldMap map = test::SyntheticMap(scale, options.seed, { .places = options.places });
    const core::RoutingRules rules{ .sectorSize = options.sector, .gateSpacing = options.gateSpacing };
    const Budgets& budgets = BudgetsOf(scale);
    core::Jobs jobs{ options.workers };
    std::vector<core::RouteGraphs> graphs; // the one serial build, kept for the tables
    const f64 parallelMs = BestOf(BUILD_RUNS, [&] {
        return core::RouteGraphs::Build(
            map, rules, { .jobs = &jobs, .sectorsPerChunk = options.chunk, .share = options.share }
        );
    });
    const f64 serialMs = BestOf(1, [&] {
        graphs.push_back(core::RouteGraphs::Build(map, rules, { .sectorsPerChunk = options.chunk, .share = options.share }));
        return true;
    });
    const World world{ .mapPointer = &map, .rules = rules, .graphsPointer = &graphs.front() };
    PN_LOG_INFO(
        "scale {} ({} x {} tiles, {} places, sector {}, gate spacing {}, chunk {}, {}, {}):",
        scale,
        map.Columns(),
        map.Rows(),
        map.Places().size(),
        options.sector,
        options.gateSpacing,
        options.chunk,
        options.share ? "shared" : "not shared",
        options.freshScratch ? "fresh workspaces" : "kept workspace"
    );

    const Tables tables = BuildTables(map, graphs.front(), jobs);
    core::RouteScratch scratch;
    Warm(world, scratch);
    core::RouteScratch* shared = options.freshScratch ? nullptr : &scratch;
    bool over = ReportBuild(options, budgets, world, tables, parallelMs, serialMs);
    over = MeasureTrips(options, budgets, world, tables, shared) || over;
    over = MeasureFarPairs(options, budgets, world, shared) || over;
    return MeasureQuality(options, scale, jobs, world) || over;
}

} // namespace

int main(int argc, char** argv) {
    core::math::SetFloatingPointEnvironment();
    std::vector<std::string_view> args;
#ifdef __clang__
#pragma clang diagnostic push
// The C runtime hands the arguments over as a pointer and a count: nothing can check them.
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
    const std::span<char*> all{ argv, static_cast<std::size_t>(argc) };
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    for (const char* arg : all | std::views::drop(1)) {
        args.emplace_back(arg);
    }
    const std::optional<Options> options = Parse(args);
    if (!options) {
        return 2;
    }
    bool over = false;
    for (const u32 scale : options->scales) {
        over = Measure(*options, scale) || over;
    }
    return over ? 1 : 0;
}
