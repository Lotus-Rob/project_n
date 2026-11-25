#include "core/place_table.hpp"

#include "core/jobs.hpp"
#include "core/profile.hpp"

namespace core {

namespace {

/// The power past which more reaches no further: a reach of a u32 per point already saturates.
constexpr f32 MOST_WHOLE_POWER = 4'294'967'296.0F;

/// How far a traveller of `power` will go: the base plus a reach per whole point of power, saturating just below
/// UNREACHABLE.
u32 ReachAt(const TravelRules& rules, f32 power) {
    // Whole points only, and a NaN or negative power has none; at most 2^32 of them, so the product fits a u64.
    const u64 whole = power > 0.0F ? static_cast<u64>(std::min(power, MOST_WHOLE_POWER)) : 0;
    const u64 reach = u64{ rules.reachBase } + (whole * rules.reachPerPower);
    return static_cast<u32>(std::min<u64>(reach, UNREACHABLE - 1));
}

/// A chunk's workspace: per node a cost and the stamp of the search that wrote it, and the queue's storage. A search
/// writes only the nodes it reaches and clears nothing.
class CityWorkspace {
public:
    /// Readies it for searches over `nodes` nodes.
    explicit CityWorkspace(std::size_t nodes)
      : m_cost(nodes, 0)
      , m_stamp(nodes, 0) {
    }

    /// Searches `graph` from `source`, keeping only costs within `bound`.
    void Search(const RouteGraph& graph, u32 source, u32 bound) {
        ++m_search;
        m_queue.clear();
        Offer(source, 0);
        while (!m_queue.empty()) {
            std::ranges::pop_heap(m_queue, std::greater<>{});
            const auto [cost, node] = m_queue.back();
            m_queue.pop_back();
            if (cost > m_cost[node]) {
                continue;
            }
            for (const GraphLink& link : graph.LinksOf(node)) {
                const u64 through = u64{ cost } + link.cost;
                if (through <= bound) {
                    Offer(link.node, static_cast<u32>(through));
                }
            }
        }
    }

    /// The cost to `node` found by the last search, or UNREACHABLE if it didn't reach it.
    [[nodiscard]] u32 CostOf(u32 node) const {
        return m_stamp[node] == m_search ? m_cost[node] : UNREACHABLE;
    }

private:
    /// Queues `node` at `cost` when that betters what this search had for it.
    void Offer(u32 node, u32 cost) {
        if (m_stamp[node] == m_search && m_cost[node] <= cost) {
            return;
        }
        m_stamp[node] = m_search;
        m_cost[node] = cost;
        m_queue.emplace_back(cost, node);
        std::ranges::push_heap(m_queue, std::greater<>{});
    }

    /// Each node's cost so far, valid where its stamp is the search's.
    std::vector<u32> m_cost;
    /// The search that last reached each node.
    std::vector<u32> m_stamp;
    /// The queue: a binary heap of (cost, node), the least first, so equal costs pop by node; stale entries are skipped.
    std::vector<std::pair<u32, u32>> m_queue;
    /// The last search's stamp; 0 marks none.
    u32 m_search = 0;
};

} // namespace

u32 ReachBound(const TravelRules& rules, ProfileIndex profile) {
    if (rules.bands.empty()) {
        return profile == WALKING ? UNREACHABLE : 0;
    }
    u32 bound = 0;
    for (std::size_t band = 0; band < rules.bands.size(); ++band) {
        if (rules.bands[band].profile != profile) {
            continue;
        }
        if (band + 1 == rules.bands.size()) {
            return UNREACHABLE;
        }
        bound = std::max(bound, ReachAt(rules, rules.bands[band + 1].fromPower));
    }
    return bound;
}

PlaceTable PlaceTable::Build(const WorldMap& map, const RouteGraph& graph, u32 bound, Jobs* jobs) {
    PN_PROFILE_ZONE();
    const std::span<const Place> places = map.Places();
    PlaceTable table;
    table.m_places = places.size();
    table.m_row.assign(places.size(), NO_NODE);
    std::vector<u16> cities;
    for (std::size_t place = 0; place < places.size(); ++place) {
        if (places[place].kind == PlaceKind::City) {
            table.m_row[place] = static_cast<u32>(cities.size());
            cities.push_back(static_cast<u16>(place));
        }
    }
    table.m_costs.assign(cities.size() * places.size(), UNREACHABLE);

    const auto searchChunk = [&](u32 chunk) {
        CityWorkspace workspace{ graph.Nodes() };
        const std::size_t first = std::size_t{ chunk } * CITIES_PER_CHUNK;
        const std::size_t last = std::min(first + CITIES_PER_CHUNK, cities.size());
        for (std::size_t row = first; row < last; ++row) {
            const u32 source = graph.PlaceNode(cities[row]);
            if (source == NO_NODE) {
                continue;
            }
            workspace.Search(graph, source, bound);
            const std::span<u32> costs = std::span{ table.m_costs }.subspan(row * places.size(), places.size());
            for (std::size_t place = 0; place < places.size(); ++place) {
                const u32 node = graph.PlaceNode(static_cast<u16>(place));
                costs[place] = node == NO_NODE ? UNREACHABLE : workspace.CostOf(node);
            }
        }
    };
    const auto chunks = static_cast<u32>((cities.size() + CITIES_PER_CHUNK - 1) / CITIES_PER_CHUNK);
    if (jobs != nullptr) {
        jobs->ParallelFor(chunks, searchChunk);
    }
    else {
        for (u32 chunk = 0; chunk < chunks; ++chunk) {
            searchChunk(chunk);
        }
    }
    return table;
}

u32 PlaceTable::Cost(u16 city, u16 place) const {
    if (city >= m_row.size() || place >= m_places || m_row[city] == NO_NODE) {
        return UNREACHABLE;
    }
    return m_costs[(std::size_t{ m_row[city] } * m_places) + place];
}

std::size_t PlaceTable::Bytes() const {
    return sizeof(*this) + CapacityBytes(m_row) + CapacityBytes(m_costs);
}

} // namespace core
