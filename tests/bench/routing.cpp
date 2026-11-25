// The routing benchmark: builds every profile's route graph over the synthetic maps and reports the build's time, the
// graphs' memory and their counts, failing over the build and memory budgets. Run it optimised, on a machine with nothing
// else running: `pixi run project_n test --mode release --target routing_bench [-- <flags>]`, the flags are listed in
// FLAG_LIST.
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/jobs.hpp"
#include "core/log.hpp"
#include "core/math.hpp"
#include "core/route_graph.hpp"
#include "support/synthetic_map.hpp"

namespace {

constexpr u32 BUILD_RUNS = 5;
constexpr f64 BUILD_BUDGET_MS_PER_SCALE = 20.0;
constexpr f64 MEMORY_BUDGET_MB_PER_SCALE = 1.0;
constexpr f64 MEGABYTE = 1024.0 * 1024.0;
constexpr std::string_view FLAG_LIST = "--scales, --sector, --gate-spacing, --workers, --seed, --chunk, --no-share";

/// The parsed flags.
struct Options {
    /// The maps' scales: 1, 10 or 50 times the test map's area.
    std::vector<u32> scales{ 1, 10, 50 };
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
};

/// Logs each row as `<name>: <measured> <unit> (budget <budget>) [OVER]` and returns whether any is over its budget.
bool Print(std::span<const Row> rows) {
    bool over = false;
    for (const Row& row : rows) {
        const bool isOver = row.budget > 0.0 && row.measured > row.budget;
        over = over || isOver;
        if (row.budget > 0.0) {
            PN_LOG_INFO(
                "{}: {:.3f} {} (budget {:.3f}){}", row.name, row.measured, row.unit, row.budget, isOver ? " [OVER]" : ""
            );
        }
        else {
            PN_LOG_INFO("{}: {:.3f} {}", row.name, row.measured, row.unit);
        }
    }
    return over;
}

/// The fastest of `runs` runs of `work`, in milliseconds.
f64 BestOf(u32 runs, const auto& work) {
    f64 best = std::numeric_limits<f64>::max();
    for (u32 run = 0; run < runs; ++run) {
        const auto start = std::chrono::steady_clock::now();
        work();
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
    return nullptr;
}

/// Sets `options.scales` from a comma-separated list of 1, 10 and 50; false when the list holds anything else.
bool SetScales(Options& options, std::string_view list) {
    std::vector<u32> scales;
    for (const auto part : std::views::split(list, ',')) {
        const auto scale = ParseNumber<u32>(std::string_view{ part.begin(), part.end() });
        if (!scale || (*scale != 1 && *scale != 10 && *scale != 50)) {
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
        if (*arg == "--no-share") {
            options.share = false;
            continue;
        }
        if (*arg != "--scales" && *arg != "--seed" && NumberField(options, *arg) == nullptr) {
            PN_LOG_ERROR("unknown flag {}; the flags are {}", *arg, FLAG_LIST);
            return std::nullopt;
        }
        if (std::next(arg) == args.end()) {
            PN_LOG_ERROR("{} needs a value", *arg);
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
    void Add(std::string name, f64 measured, f64 budget, std::string_view unit) {
        m_names.push_back(std::move(name));
        m_rows.push_back({ .name = {}, .measured = measured, .budget = budget, .unit = unit });
    }
    /// Logs the rows; whether any is over its budget.
    [[nodiscard]] bool Print() {
        for (std::size_t i = 0; i < m_rows.size(); ++i) {
            m_rows[i].name = m_names[i]; // views taken once the names no longer move
        }
        return ::Print(m_rows);
    }

private:
    /// The rows' names.
    std::vector<std::string> m_names;
    /// The rows, their names set when printed.
    std::vector<Row> m_rows;
};

/// How many edges of `graph` join nodes of different sectors.
u64 CrossingEdges(const core::WorldMap& map, core::RoutingRules rules, const core::RouteGraph& graph) {
    u64 crossings = 0;
    for (const core::GraphEdge& edge : graph.Edges()) {
        const u32 from = core::SectorOf(map, rules, graph.NodeTile(edge.from));
        crossings += from == core::SectorOf(map, rules, graph.NodeTile(edge.to)) ? 0 : 1;
    }
    return crossings;
}

/// Builds the graphs of one scale and logs the three tables; returns whether a build or memory row is over.
bool Measure(const Options& options, u32 scale) {
    const core::WorldMap map = test::SyntheticMap(scale, options.seed);
    const core::RoutingRules rules{ .sectorSize = options.sector, .gateSpacing = options.gateSpacing };
    core::Jobs jobs{ options.workers };
    std::optional<core::RouteGraphs> graphs;
    const f64 parallelMs = BestOf(BUILD_RUNS, [&] {
        graphs.emplace(
            core::RouteGraphs::Build(map, rules, { .jobs = &jobs, .sectorsPerChunk = options.chunk, .share = options.share })
        );
    });
    const f64 serialMs = BestOf(1, [&] {
        graphs.emplace(core::RouteGraphs::Build(map, rules, { .sectorsPerChunk = options.chunk, .share = options.share }));
    });
    if (!graphs) {
        return true;
    }

    const f64 sectors = static_cast<f64>(core::SectorColumns(map, rules)) * static_cast<f64>(core::SectorRows(map, rules));
    const f64 factor = static_cast<f64>(scale);
    const std::span<const core::MovementProfile> profiles = map.Profiles();
    PN_LOG_INFO(
        "scale {} ({} x {} tiles, sector {}, gate spacing {}, chunk {}, {}):",
        scale,
        map.Columns(),
        map.Rows(),
        options.sector,
        options.gateSpacing,
        options.chunk,
        options.share ? "shared" : "not shared"
    );

    Table build;
    build.Add(std::format("build, {} workers", options.workers), parallelMs, BUILD_BUDGET_MS_PER_SCALE * factor, "ms");
    build.Add("build, no job system", serialMs, 0.0, "ms");
    bool over = build.Print();

    Table memory;
    memory.Add("memory, all graphs", static_cast<f64>(graphs->Bytes()) / MEGABYTE, MEMORY_BUDGET_MB_PER_SCALE * factor, "MB");
    for (std::size_t profile = 0; profile < profiles.size(); ++profile) {
        const core::RouteGraph& graph = graphs->Of(static_cast<core::ProfileIndex>(profile));
        memory.Add(std::format("memory, {}", profiles[profile].key), static_cast<f64>(graph.Bytes()) / MEGABYTE, 0.0, "MB");
    }
    over = memory.Print() || over;

    Table counts;
    counts.Add("sectors", sectors, 0.0, "");
    counts.Add("sectors x profiles", sectors * static_cast<f64>(profiles.size()), 0.0, "");
    counts.Add("sectors built, the rest copied", static_cast<f64>(graphs->SectorsBuilt()), 0.0, "");
    for (std::size_t profile = 0; profile < profiles.size(); ++profile) {
        const core::RouteGraph& graph = graphs->Of(static_cast<core::ProfileIndex>(profile));
        const std::string& key = profiles[profile].key;
        counts.Add(std::format("{} nodes", key), static_cast<f64>(graph.Nodes()), 0.0, "");
        counts.Add(std::format("{} edges", key), static_cast<f64>(graph.Edges().size()), 0.0, "");
        counts.Add(
            std::format("{} gates a sector", key), static_cast<f64>(CrossingEdges(map, rules, graph)) / sectors, 0.0, ""
        );
    }
    static_cast<void>(counts.Print());
    return over;
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
