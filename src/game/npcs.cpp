#include "game/npcs.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"
#include "core/random.hpp"
#include "core/schedule.hpp"
#include "game/components.hpp"

namespace game {

namespace {

// Random-number purposes (core::Random): one per use, so no two uses share numbers.
constexpr u32 PURPOSE_WANDER = 1;
constexpr u32 PURPOSE_CHOICE = 2;
constexpr u32 PURPOSE_PARTNER = 3;

constexpr f32 WALK_SPEED = 1.5F;  // world units per tick
constexpr f32 WANDER_SPAN = 0.8F; // of a cell: active NPCs pick points this far across their cell
constexpr f32 JITTER = 0.1F;      // how much chance weighs against need in a choice
constexpr f32 MONTHLY_FATIGUE = 0.1F;
constexpr f32 MONTHLY_LONELINESS = 0.1F;
constexpr f32 CULTIVATION_FATIGUE = 0.3F;
constexpr f32 CULTIVATION_DRIVE = 0.6F;
constexpr f32 COMPANY_FROM_MEETING = 0.2F;
constexpr f32 HALF = 0.5F;

f32 Unit(u64 random) {
    return static_cast<f32>(core::ToUnit(random));
}

f32 Clamped(f32 need) {
    return std::clamp(need, 0.0F, 1.0F);
}

} // namespace

core::Vec2 CellCentre(u32 cell) {
    const u32 column = cell % MAP_COLUMNS;
    const u32 row = cell / MAP_COLUMNS;
    return {
        .x = (static_cast<f32>(column) + HALF) * CELL_SIZE,
        .y = (static_cast<f32>(row) + HALF) * CELL_SIZE,
    };
}

u32 CellAt(core::Vec2 point) {
    const auto column = static_cast<u32>(std::clamp(point.x / CELL_SIZE, 0.0F, static_cast<f32>(MAP_COLUMNS - 1)));
    const auto row = static_cast<u32>(std::max(point.y / CELL_SIZE, 0.0F));
    return (row * MAP_COLUMNS) + column;
}

std::span<const Relationship> Relationships::Of(core::StableId holder) const {
    const auto [first, last] = std::ranges::equal_range(m_edges, holder, {}, &Relationship::holder);
    return std::span{ m_edges }.subspan(
        static_cast<std::size_t>(first - m_edges.begin()), static_cast<std::size_t>(last - first)
    );
}

void Relationships::Merge(std::span<const std::pair<core::StableId, core::StableId>> changes) {
    m_merged.clear();
    auto edge = m_edges.begin();
    for (const auto& [holder, subject] : changes) {
        const auto key = [](const Relationship& relationship) {
            return std::pair{ relationship.holder, relationship.subject };
        };
        while (edge != m_edges.end() && key(*edge) < std::pair{ holder, subject }) {
            m_merged.push_back(*edge++);
        }
        if (m_merged.empty() || key(m_merged.back()) != std::pair{ holder, subject }) {
            m_merged.push_back(
                edge != m_edges.end() && key(*edge) == std::pair{ holder, subject }
                    ? *edge++
                    : Relationship{ .holder = holder, .subject = subject }
            );
        }
        m_merged.back().opinion += 1.0F;
    }
    m_merged.insert(m_merged.end(), edge, m_edges.end());
    std::swap(m_edges, m_merged);
}

void Relationships::Forget(core::StableId npc) {
    std::erase_if(m_edges, [npc](const Relationship& edge) { return edge.holder == npc || edge.subject == npc; });
}

core::StableId SpawnNpc(core::State& state, u32 cell) {
    const auto [stableId, entity] = state.Create(WORLD);
    state[WORLD].Entities().emplace<Npc>(entity, cell);
    state[WORLD].Entities().emplace<Needs>(entity);
    return stableId;
}

core::StableId SpawnGroup(core::State& state, u32 cell, u32 members) {
    const auto [stableId, entity] = state.Create(WORLD);
    state[WORLD].Entities().emplace<Group>(entity, cell, members);
    return stableId;
}

void Activate(core::State& state, core::StableId npc) {
    state.Move(npc, ACTIVE, [](const core::Registry& from, core::Entity old, core::Registry& into, core::Entity moved) {
        const Npc& person = into.emplace<Npc>(moved, from.get<Npc>(old));
        into.emplace<Needs>(moved, from.get<Needs>(old));
        if (from.all_of<Met>(old)) {
            into.emplace<Met>(moved);
        }
        const core::Vec2 foot = CellCentre(person.cell);
        into.emplace<Body>(moved, foot, foot);
        into.emplace<Look>(moved, Look{ .sprite = core::atlas::SpriteId::Cultivator, .shadow = true });
        into.emplace<Wander>(moved, foot);
    });
}

void Deactivate(core::State& state, core::StableId npc) {
    state.Move(npc, WORLD, [](const core::Registry& from, core::Entity old, core::Registry& into, core::Entity moved) {
        Npc& person = into.emplace<Npc>(moved, from.get<Npc>(old));
        person.cell = CellAt(from.get<Body>(old).current);
        into.emplace<Needs>(moved, from.get<Needs>(old));
        if (from.all_of<Met>(old)) {
            into.emplace<Met>(moved);
        }
    });
}

std::vector<core::StableId> Materialise(core::State& state, core::StableId group) {
    const Group people = state[WORLD].Entities().get<Group>(state[WORLD].Find(group));
    state.Destroy(group);
    std::vector<core::StableId> npcs;
    npcs.reserve(people.members);
    for (u32 member = 0; member < people.members; ++member) {
        npcs.push_back(SpawnNpc(state, people.cell));
    }
    return npcs;
}

void Fold(core::State& state, Relationships& relationships, core::StableId npc, core::StableId group) {
    core::Registry& world = state[WORLD].Entities();
    PN_ASSERT(!world.all_of<Met>(state[WORLD].Find(npc)), "an NPC the player has met folded into a group");
    ++world.get<Group>(state[WORLD].Find(group)).members;
    relationships.Forget(npc);
    state.Destroy(npc);
}

void DecideActive(core::Domain& active, u64 seed, u64 stateCounter, u64 simulationTick) {
    PN_PROFILE_ZONE();
    for (auto [entity, stableId, person, wander] : active.Entities().view<const core::StableId, const Npc, Wander>().each()) {
        if (!core::InSlice(DECISION_TICKS, simulationTick, stableId)) {
            continue;
        }
        const u64 key = std::to_underlying(stableId);
        const core::Vec2 offset{
            .x = Unit(core::Random(seed, stateCounter, key, PURPOSE_WANDER, 0)) - HALF,
            .y = Unit(core::Random(seed, stateCounter, key, PURPOSE_WANDER, 1)) - HALF,
        };
        wander.target = CellCentre(person.cell) + (offset * (WANDER_SPAN * CELL_SIZE));
    }
}

void WalkActive(core::Domain& active) {
    PN_PROFILE_ZONE();
    for (auto [entity, body, wander] : active.Entities().view<Body, const Wander>().each()) {
        body.previous = body.current;
        const core::Vec2 way = wander.target - body.current;
        const f32 distance = std::sqrt((way.x * way.x) + (way.y * way.y));
        body.current = distance <= WALK_SPEED ? wander.target : body.current + (way * (WALK_SPEED / distance));
    }
}

void MonthStep::Run(core::State& state, Relationships& relationships, core::Jobs& jobs, u64 stateCounter) {
    PN_PROFILE_ZONE();
    core::Registry& world = state[WORLD].Entities();
    auto& npcs = world.storage<Npc>();
    auto& needs = world.storage<Needs>();
    const auto& ids = world.storage<core::StableId>();
    const u64 seed = state.Seed();

    m_byCell.clear();
    for (const auto [entity, person] : npcs.each()) {
        m_byCell.emplace_back(person.cell, ids.get(entity));
    }
    std::ranges::sort(m_byCell);

    const u32 chunks = core::ChunkCount(npcs.size(), CHUNK);
    m_met.resize(std::max<std::size_t>(m_met.size(), chunks));
    jobs.ParallelFor(chunks, [&](u32 chunk) {
        std::vector<std::pair<core::StableId, core::StableId>>& met = m_met[chunk];
        met.clear();
        const std::size_t begin = std::size_t{ chunk } * CHUNK;
        const std::size_t end = std::min(npcs.size(), begin + CHUNK);
        for (std::size_t i = begin; i < end; ++i) {
            const core::Entity entity = npcs[i];
            Npc& person = npcs.get(entity);
            Needs& want = needs.get(entity);
            const core::StableId self = ids.get(entity);
            const u64 key = std::to_underlying(self);
            const auto chance = [&](Action action) {
                return JITTER * Unit(core::Random(seed, stateCounter, key, PURPOSE_CHOICE, std::to_underlying(action)));
            };
            const std::array<std::pair<f32, Action>, 3> scores{ {
                { want.rest + chance(Action::Rest), Action::Rest },
                { (CULTIVATION_DRIVE * (1.0F - want.rest)) + chance(Action::Cultivate), Action::Cultivate },
                { want.company + chance(Action::Socialise), Action::Socialise },
            } };
            person.action = std::ranges::max(scores, {}, &std::pair<f32, Action>::first).second;

            if (person.action == Action::Socialise) {
                // Everyone else in its cell: the cell's range of m_byCell without itself.
                const auto [first, last] =
                    std::ranges::equal_range(m_byCell, person.cell, {}, &std::pair<u32, core::StableId>::first);
                const auto others = static_cast<u32>(last - first) - 1;
                if (others == 0) {
                    person.action = Action::Rest;
                }
                else {
                    auto partner = first + core::ToBelow(core::Random(seed, stateCounter, key, PURPOSE_PARTNER, 0), others);
                    if (partner->second >= self) {
                        ++partner; // skips itself: the range is sorted by stable ID within the cell
                    }
                    met.emplace_back(partner->second, self);
                    want.company = 0.0F;
                }
            }
            if (person.action == Action::Rest) {
                want.rest = 0.0F;
            }
            if (person.action == Action::Cultivate) {
                person.power += 1.0F;
                want.rest += CULTIVATION_FATIGUE;
            }
            want.rest = Clamped(want.rest + MONTHLY_FATIGUE);
            want.company = Clamped(want.company + MONTHLY_LONELINESS);
        }
    });

    // Chunks in order, then a stable sort by target and source: emission order breaks ties.
    m_merged.clear();
    for (u32 chunk = 0; chunk < chunks; ++chunk) {
        m_merged.insert(m_merged.end(), m_met[chunk].begin(), m_met[chunk].end());
    }
    std::ranges::stable_sort(m_merged);
    for (const auto& [target, source] : m_merged) {
        Needs& want = needs.get(state[WORLD].Find(target));
        want.company = Clamped(want.company - COMPANY_FROM_MEETING);
    }
    relationships.Merge(m_merged);
    PN_LOG_DEBUG("Month step: {} world NPCs, {} meetings", npcs.size(), m_merged.size());
}

} // namespace game
