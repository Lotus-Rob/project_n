#pragma once

/// @file
/// @ingroup core
/// Routes over the world map (phase 6 chunk 5b2): a distance map gives, from every tile, what walking to one goal costs
/// by a movement profile; a route follows it down, or A* finds one tile to tile. Cost is whole numbers with fixed
/// tie-breaks (lower tile index), so every route is the same on every machine.

#include "core/types.hpp"
#include "core/world_map.hpp"

#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace core {

/// The cost of a tile that can't reach the goal.
inline constexpr u32 UNREACHABLE = std::numeric_limits<u32>::max();

/// What walking from each tile to one goal costs by one profile. It searches backwards from the goal, relaxing a
/// neighbour n by StepCost(n, current): the cost of stepping from n into current, which differs from the way back
/// (entering costs differ each way).
class DistanceMap {
public:
    /// The costs to `goal` by `profile`; a goal no step can enter has only itself (cost 0) reaching it.
    [[nodiscard]] static DistanceMap Build(const WorldMap& map, u32 goal, ProfileIndex profile);

    /// What walking from `tile` to the goal costs, or UNREACHABLE.
    [[nodiscard]] u32 CostFrom(u32 tile) const {
        return m_cost.at(tile);
    }
    /// The goal's tile.
    [[nodiscard]] u32 Goal() const {
        return m_goal;
    }
    /// The profile the costs are by.
    [[nodiscard]] ProfileIndex Profile() const {
        return m_profile;
    }

private:
    DistanceMap(std::vector<u32> cost, u32 goal, ProfileIndex profile)
      : m_cost{ std::move(cost) }
      , m_goal{ goal }
      , m_profile{ profile } {
    }

    std::vector<u32> m_cost;
    u32 m_goal;
    ProfileIndex m_profile;
};

/// The tiles from `from` down `distances` to its goal, both included: from each tile it steps to the neighbour n with
/// StepCost(tile, n) + CostFrom(n) == CostFrom(tile), the lowest tile index on a tie. `{from}` when `from` is the goal;
/// empty when the goal is unreachable from it.
[[nodiscard]] std::vector<u32> RouteDown(const WorldMap& map, const DistanceMap& distances, u32 from);

/// The cheapest route from `from` to `target` by `profile` (A*, ties by lower tile index), with RouteDown's contract: its
/// steps cost what the distance map to `target` says. Empty when `target` is impassable or unreachable.
[[nodiscard]] std::vector<u32> FindRoute(const WorldMap& map, ProfileIndex profile, u32 from, u32 target);

/// The distance maps to the map's cities, each built on first use and kept. Not thread-safe: Warm before any parallel
/// reads.
class Paths {
public:
    /// Routes over `map`.
    explicit Paths(std::shared_ptr<const WorldMap> map);

    /// The distance map to the city with place index `city` by `profile`, built on first use; `city` must be a city.
    [[nodiscard]] const DistanceMap& To(u16 city, ProfileIndex profile);
    /// Builds every city's map by `profile`.
    void Warm(ProfileIndex profile);

private:
    std::shared_ptr<const WorldMap> m_map;
    // One slot per (profile, place); slots never move, so a returned reference stays valid.
    std::vector<std::vector<std::optional<DistanceMap>>> m_maps;
};

} // namespace core
