#include "game/travel.hpp"

#include "core/assert.hpp"
#include "core/calendar.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"
#include "core/random.hpp"
#include "game/npcs.hpp"
#include "game/random_purposes.hpp"

namespace game {

namespace {

/// A day in a journey's signed ticks.
constexpr i64 DAY_TICKS = static_cast<i64>(core::TICKS_PER_DAY);
/// The most days a journal entry counts away.
constexpr i64 MOST_DAYS_AWAY = std::numeric_limits<u8>::max();

/// The day `tick` falls in, counted from tick 0: rounded down, so the ticks just before 0 are in day -1.
i64 DayOf(i64 tick) {
    const i64 day = tick / DAY_TICKS;
    return tick % DAY_TICKS < 0 ? day - 1 : day;
}

/// The journal entry for `journey`, which has parts.
JournalEntry EntryOf(const core::Journey& journey) {
    const i64 departure = DayOf(core::StartTick(journey));
    const i64 away = DayOf(core::EndTick(journey)) - departure;
    const auto leg = std::ranges::find(journey.parts, false, &core::JourneyPart::stay);
    return {
        .destination = journey.destination,
        .departureDay =
            static_cast<i32>(std::clamp<i64>(departure, std::numeric_limits<i32>::min(), std::numeric_limits<i32>::max())),
        .daysAway = static_cast<u8>(std::clamp<i64>(away, 0, MOST_DAYS_AWAY)),
        .profile = leg == journey.parts.end() ? core::WALKING : leg->leg.profile,
    };
}

/// How many entries `npc`'s journal keeps: more for an NPC the player has met.
std::size_t JournalLength(const core::State& state, core::StableId npc) {
    const std::optional<core::DomainIndex> domain = state.DomainOf(npc);
    const bool met = domain && state[*domain].Entities().all_of<Met>(state[*domain].Find(npc));
    return met ? MET_JOURNAL_LENGTH : JOURNAL_LENGTH;
}

/// Whether `place` is a city of `map`.
bool IsCity(const core::WorldMap& map, u16 place) {
    return place < map.Places().size() && map.Places()[place].kind == core::PlaceKind::City;
}

/// Whether `npc` is a world NPC of `state`.
bool IsWorldNpc(const core::State& state, core::StableId npc) {
    return state.DomainOf(npc) == WORLD && state[WORLD].Entities().all_of<Npc>(state[WORLD].Find(npc));
}

/// Whether `stay`, read from a save, can be placed on `map`: in a city, ending at or after it starts, lasting no more
/// ticks than a leg's arrive ticks hold, and ending at least a day before an i64's most, so placing its walker (whose
/// last day runs a day past its start) never overflows.
bool StaySound(const core::Stay& stay, const core::WorldMap& map) {
    if (!IsCity(map, stay.city) || stay.endTick < stay.startTick ||
        stay.endTick > std::numeric_limits<i64>::max() - DAY_TICKS) {
        return false;
    }
    // Ordered, the difference of two i64s fits a u64.
    const u64 span = static_cast<u64>(stay.endTick) - static_cast<u64>(stay.startTick);
    return span <= std::numeric_limits<u32>::max();
}

/// Whether `part`, read from a save, can be placed on `map`.
bool PartSound(const core::JourneyPart& part, const core::WorldMap& map) {
    return part.stay ? StaySound(part.rest, map) : LegSound(part.leg, map);
}

/// Whether `journey`, read from a save, suits `state` and `map`: members, all world NPCs, between two cities, and parts,
/// each sound and starting on the tick the one before ends.
bool JourneySound(const core::Journey& journey, const core::State& state, const core::WorldMap& map) {
    const auto gap = [](const core::JourneyPart& before, const core::JourneyPart& after) {
        return core::StartTick(after) != core::EndTick(before);
    };
    return !journey.members.empty() &&
           std::ranges::all_of(journey.members, [&state](core::StableId npc) { return IsWorldNpc(state, npc); }) &&
           IsCity(map, journey.home) && IsCity(map, journey.destination) && !journey.parts.empty() &&
           std::ranges::all_of(journey.parts, [&map](const core::JourneyPart& part) { return PartSound(part, map); }) &&
           std::ranges::adjacent_find(journey.parts, gap) == journey.parts.end();
}

core::StableId Leader(const core::Journey& journey) {
    return journey.members.front();
}

// A destination's weight in fixed point: the pull's people per unit of cost squared, and the reach's share left of it.
constexpr u64 PULL_SCALE = 1'000'000;
constexpr u64 REACH_SCALE = 1'000;
// The travel share's whole, and the power past which more reaches no further: a reach of a u32 per point already saturates.
constexpr u32 SHARE_WHOLE = 1'000;
constexpr f32 MOST_WHOLE_POWER = 4'294'967'296.0F;
constexpr u64 MOST_WEIGHT = std::numeric_limits<u64>::max();

// The numbers an NPC draws in a month under its key (core::Random's draw index).
constexpr u32 VISIT_DRAW_SETS_OUT = 0;
constexpr u32 VISIT_DRAW_DEPARTURE = 1;
constexpr u32 VISIT_DRAW_STAY = 2;
constexpr u32 VISIT_DRAW_DESTINATION = 3;

u64 SaturatingSum(u64 lhs, u64 rhs) {
    return lhs > MOST_WEIGHT - rhs ? MOST_WEIGHT : lhs + rhs;
}

u64 SaturatingProduct(u64 lhs, u64 rhs) {
    return lhs != 0 && rhs > MOST_WEIGHT / lhs ? MOST_WEIGHT : lhs * rhs;
}

/// What one month of visits reads, shared by its chunks.
struct VisitMonth {
    const core::WorldMap* map = nullptr;
    core::Paths* paths = nullptr;
    const Travel* travel = nullptr;
    u64 key = 0; // the month's, under which each NPC draws
    i64 monthStart = 0;
};

/// The visits the world NPCs of chunk `chunk` (MonthStep::CHUNK of them, in the order of `npcs`, the world's Npc storage,
/// whose stable IDs `ids` holds) set out on in `month`, each appended to `out`, its member's month made Action::Travel.
void SetOut(const VisitMonth& month, auto& npcs, const auto& ids, u32 chunk, std::vector<core::Journey>& out) {
    const core::WorldMap& map = *month.map;
    const core::TravelRules& rules = map.Travel();
    const u32 stayChoices = u32{ rules.stayMaxDays } - u32{ rules.stayMinDays } + 1;
    const std::size_t begin = std::size_t{ chunk } * MonthStep::CHUNK;
    const std::size_t end = std::min(npcs.size(), begin + MonthStep::CHUNK);
    for (std::size_t i = begin; i < end; ++i) {
        const core::Entity entity = npcs[i];
        Npc& person = npcs.get(entity);
        const core::StableId npc = ids.get(entity);
        if (!IsCity(map, person.home) || month.travel->Travelling(npc)) {
            continue;
        }
        // The way back would end by stepping onto this tile, which walkers can't enter: such an NPC has no way home.
        if (map.CostAt(person.cell, core::WALKING) == core::IMPASSABLE_COST) {
            continue;
        }
        const u64 key = std::to_underlying(npc);
        const auto draw = [&month, key](u32 index) { return core::Random(month.key, 0, key, PURPOSE_VISIT, index); };
        if (core::ToBelow(draw(VISIT_DRAW_SETS_OUT), SHARE_WHOLE) >= rules.sharePerMille) {
            continue;
        }
        const std::optional<u16> destination =
            ChooseDestination(map, rules, *month.paths, person, draw(VISIT_DRAW_DESTINATION));
        if (!destination) {
            continue;
        }
        const std::vector<u32> route = core::RouteDown(map, month.paths->To(*destination, core::WALKING), person.cell);
        PN_ASSERT(!route.empty(), "a city with a weight has a route to it");
        if (route.empty()) {
            continue;
        }
        const i64 departure =
            month.monthStart + i64{ core::ToBelow(draw(VISIT_DRAW_DEPARTURE), static_cast<u32>(core::TICKS_PER_MONTH)) };
        const u32 stayDays = u32{ rules.stayMinDays } + core::ToBelow(draw(VISIT_DRAW_STAY), stayChoices);
        const auto seed = static_cast<u32>(core::Random(month.key, 0, key, PURPOSE_JOURNEY_SEED, 0));
        out.push_back(core::Visit(map, route, npc, person.home, *destination, departure, stayDays, seed));
        person.action = Action::Travel;
    }
}

} // namespace

bool LegSound(const core::Leg& leg, const core::WorldMap& map) {
    const u32 tiles = map.Columns() * map.Rows();
    return leg.shape <= core::LegShape::TileRoute && leg.profile < map.Profiles().size() && core::IsFinite(leg.origin) &&
           !leg.tiles.empty() && leg.arrive.size() == leg.tiles.size() && leg.arrive.front() == 0 &&
           std::ranges::adjacent_find(leg.arrive, std::greater_equal{}) == leg.arrive.end() &&
           std::ranges::all_of(
               leg.tiles, [tiles](u32 tile) { return tile < tiles; }
           ) &&
           leg.startTick <= std::numeric_limits<i64>::max() - i64{ leg.arrive.back() };
}

std::expected<void, core::Error> SetTravelShare(core::GameData& data, u16 sharePerMille) {
    if (data.world == nullptr) {
        return std::unexpected{ core::Error{ "the travel share needs the world map, which the game data lacks" } };
    }
    core::TravelRules rules = data.world->Travel();
    rules.sharePerMille = sharePerMille;
    std::expected<core::WorldMap, core::Error> raised = data.world->WithTravel(rules);
    if (!raised) {
        return std::unexpected{ raised.error() };
    }
    data.world = std::make_shared<const core::WorldMap>(*std::move(raised));
    return {};
}

core::VisitorKind VisitorOf(const core::TravelRules& rules, f32 power) {
    return power >= rules.cultivatorPower ? core::VisitorKind::Cultivator : core::VisitorKind::Trader;
}

u64 DestinationWeight(const core::WorldMap& map, const core::TravelRules& rules, f32 power, u32 cost, u16 destination) {
    if (cost == core::UNREACHABLE) {
        return 0;
    }
    const core::Place& city = map.Places()[destination];
    // Below UNREACHABLE, a cost squared fits a u64.
    const u64 steps = std::max<u64>(cost, 1);
    const u64 pull = u64{ city.people } * PULL_SCALE / (steps * steps);
    const u64 whole = power > 0.0F ? static_cast<u64>(std::min(power, MOST_WHOLE_POWER)) : 0;
    const u64 reachCost = SaturatingSum(rules.reachBase, SaturatingProduct(whole, rules.reachPerPower));
    if (reachCost <= cost) {
        return 0;
    }
    // (R - cost) * 1,000 / R rounded down is 1,000 less cost * 1,000 / R rounded up, which no reach overflows.
    const u64 spent = u64{ cost } * REACH_SCALE;
    const u64 reach = REACH_SCALE - ((spent / reachCost) + (spent % reachCost == 0 ? 0 : 1));
    const u16 character = city.draws.at(std::to_underlying(VisitorOf(rules, power)));
    return SaturatingProduct(SaturatingProduct(pull, reach), character);
}

std::optional<u16> ChooseDestination(
    const core::WorldMap& map,
    const core::TravelRules& rules,
    core::Paths& paths,
    const Npc& npc,
    u64 random
) {
    const auto weightOf = [&](std::size_t place) -> u64 {
        if (place == npc.home || !IsCity(map, static_cast<u16>(place))) {
            return 0;
        }
        const auto city = static_cast<u16>(place);
        return DestinationWeight(map, rules, npc.power, paths.To(city, core::WALKING).CostFrom(npc.cell), city);
    };
    const std::size_t places = map.Places().size();
    u64 total = 0;
    for (std::size_t place = 0; place < places; ++place) {
        total = SaturatingSum(total, weightOf(place));
    }
    if (total == 0) {
        return std::nullopt;
    }
    const u64 pick = core::ToBelowWide(random, total);
    u64 cumulative = 0;
    for (std::size_t place = 0; place < places; ++place) {
        const u64 weight = weightOf(place);
        cumulative = SaturatingSum(cumulative, weight);
        if (weight != 0 && pick < cumulative) {
            return static_cast<u16>(place);
        }
    }
    return std::nullopt; // not reached: the pick lies below the total, which the last city's sum reaches
}

void Travel::Start(core::Journey journey) {
    PN_ASSERT(!journey.members.empty() && !journey.parts.empty(), "a journey has a member and a part");
    PN_ASSERT(
        std::ranges::none_of(journey.members, [this](core::StableId npc) { return Travelling(npc); }),
        "a journey's members aren't travelling already"
    );
    if (journey.members.empty()) {
        return; // with assertions off, a journey nobody makes is dropped: it has no leader to keep it in order by
    }
    // One without parts is kept: it ends at tick 0 (core::EndTick()), so the next Finish() writes it and drops it.
    m_travellers.insert(journey.members.begin(), journey.members.end());
    m_journeys.insert(std::ranges::upper_bound(m_journeys, Leader(journey), {}, Leader), std::move(journey));
}

bool Travel::Travelling(core::StableId npc) const {
    return m_travellers.contains(npc);
}

void Travel::Finish(i64 tick, core::State& state) {
    m_written.clear();
    for (const core::Journey& journey : m_journeys) {
        if (core::EndTick(journey) > tick) {
            continue;
        }
        const JournalEntry entry = EntryOf(journey);
        for (const core::StableId npc : journey.members) {
            m_written.emplace_back(npc, entry);
            PN_ASSERT(IsWorldNpc(state, npc), "a traveller is a world NPC");
            if (IsWorldNpc(state, npc)) {
                state[WORLD].Entities().get<Npc>(state[WORLD].Find(npc)).action = Action::Rest;
            }
            m_travellers.erase(npc);
        }
    }
    if (m_written.empty()) {
        return;
    }
    const std::size_t ended =
        std::erase_if(m_journeys, [tick](const core::Journey& journey) { return core::EndTick(journey) <= tick; });
    PN_LOG_DEBUG("{} journeys ended by world tick {}", ended, tick);

    // Merged in one pass rather than inserted one by one: tens of thousands of journals, thousands of entries a month.
    std::ranges::stable_sort(m_written, {}, &std::pair<core::StableId, JournalEntry>::first);
    m_merged.clear();
    auto journal = m_journals.begin();
    for (auto written = m_written.begin(); written != m_written.end();) {
        const core::StableId npc = written->first;
        while (journal != m_journals.end() && journal->npc < npc) {
            m_merged.push_back(std::move(*journal++));
        }
        if (journal != m_journals.end() && journal->npc == npc) {
            m_merged.push_back(std::move(*journal++));
        }
        else {
            m_merged.push_back({ .npc = npc, .entries = {} });
        }
        std::vector<JournalEntry>& entries = m_merged.back().entries;
        for (; written != m_written.end() && written->first == npc; ++written) {
            entries.push_back(written->second);
        }
        const std::size_t length = JournalLength(state, npc);
        if (entries.size() > length) {
            entries.erase(entries.begin(), entries.end() - static_cast<std::ptrdiff_t>(length));
        }
    }
    std::move(journal, m_journals.end(), std::back_inserter(m_merged));
    std::swap(m_journals, m_merged);
    m_merged.clear();
}

void Travel::StartAll(std::vector<core::Journey>& journeys) {
    PN_ASSERT(
        std::ranges::all_of(journeys, [](const core::Journey& journey) { return !journey.members.empty(); }),
        "a journey has a member"
    );
    std::erase_if(journeys, [](const core::Journey& journey) { return journey.members.empty(); });
    std::ranges::sort(journeys, {}, Leader); // no one leads two, so the order is total
    for (const core::Journey& journey : journeys) {
        PN_ASSERT(
            std::ranges::none_of(journey.members, [this](core::StableId npc) { return Travelling(npc); }),
            "a journey's members aren't travelling already"
        );
        m_travellers.insert(journey.members.begin(), journey.members.end());
    }
    const auto before = static_cast<std::ptrdiff_t>(m_journeys.size());
    m_journeys.insert(m_journeys.end(), std::make_move_iterator(journeys.begin()), std::make_move_iterator(journeys.end()));
    std::ranges::inplace_merge(m_journeys, m_journeys.begin() + before, {}, Leader);
    journeys.clear();
}

void Travel::SendOut(
    core::State& state,
    const core::WorldMap& map,
    core::Paths& paths,
    core::Jobs* jobs,
    u64 seed,
    u64 stateCounter,
    i64 monthStart
) {
    // Built here, before the chunks read them at once.
    paths.Warm(core::WALKING);
    // Fetched once, before the fork: the chunks then only read and write within them.
    core::Registry& world = state[WORLD].Entities();
    auto& npcs = world.storage<Npc>();
    const auto& ids = world.storage<core::StableId>();
    const u32 chunks = core::ChunkCount(npcs.size(), MonthStep::CHUNK);
    m_setOut.resize(std::max<std::size_t>(m_setOut.size(), chunks));
    const VisitMonth month{
        .map = &map,
        .paths = &paths,
        .travel = this,
        .key = core::Random(seed, stateCounter, std::bit_cast<u64>(monthStart), PURPOSE_VISIT_MONTH, 0),
        .monthStart = monthStart,
    };
    const auto setOut = [&](u32 chunk) {
        m_setOut[chunk].clear();
        SetOut(month, npcs, ids, chunk, m_setOut[chunk]);
    };
    if (jobs != nullptr) {
        jobs->ParallelFor(chunks, setOut);
    }
    else {
        for (u32 chunk = 0; chunk < chunks; ++chunk) {
            setOut(chunk);
        }
    }
    m_departing.clear();
    for (u32 chunk = 0; chunk < chunks; ++chunk) {
        std::ranges::move(m_setOut[chunk], std::back_inserter(m_departing));
        m_setOut[chunk].clear();
    }
    PN_LOG_DEBUG("{} world NPCs set out on visits in the month from world tick {}", m_departing.size(), monthStart);
    StartAll(m_departing);
}

void Travel::Month(
    core::State& state,
    const core::WorldMap& map,
    core::Paths& paths,
    core::Jobs& jobs,
    u64 seed,
    u64 stateCounter,
    i64 monthStart
) {
    PN_PROFILE_ZONE();
    SendOut(state, map, paths, &jobs, seed, stateCounter, monthStart);
}

void Travel::Seed(
    core::State& state,
    const core::WorldMap& map,
    core::Paths& paths,
    u64 seed,
    u64 stateCounter,
    i64 monthStart
) {
    PN_PROFILE_ZONE();
    SendOut(state, map, paths, nullptr, seed, stateCounter, monthStart - static_cast<i64>(core::TICKS_PER_MONTH));
    Finish(monthStart, state);
    SendOut(state, map, paths, nullptr, seed, stateCounter, monthStart);
}

std::span<const JournalEntry> Travel::JournalOf(core::StableId npc) const {
    const auto found = std::ranges::lower_bound(m_journals, npc, {}, &Journal::npc);
    if (found == m_journals.end() || found->npc != npc) {
        return {};
    }
    return found->entries;
}

void Travel::Forget(core::StableId npc) {
    if (const auto journal = std::ranges::lower_bound(m_journals, npc, {}, &Journal::npc);
        journal != m_journals.end() && journal->npc == npc) {
        m_journals.erase(journal);
    }
    if (m_travellers.erase(npc) == 0) {
        return;
    }
    // Led by `npc`, its journey is found by its key; a follower's needs the scan.
    auto found = std::ranges::lower_bound(m_journeys, npc, {}, Leader);
    if (found == m_journeys.end() || Leader(*found) != npc) {
        found = std::ranges::find_if(m_journeys, [npc](const core::Journey& journey) {
            return std::ranges::contains(journey.members, npc);
        });
    }
    PN_ASSERT(found != m_journeys.end(), "a traveller is a member of a journey");
    if (found == m_journeys.end()) {
        return;
    }
    const bool led = Leader(*found) == npc;
    std::erase(found->members, npc);
    if (found->members.empty()) {
        m_journeys.erase(found);
    }
    else if (led) {
        // Keyed by its next member now, it moves to that member's place.
        core::Journey journey = std::move(*found);
        m_journeys.erase(found);
        m_journeys.insert(std::ranges::upper_bound(m_journeys, Leader(journey), {}, Leader), std::move(journey));
    }
}

bool Travel::Load(core::Reader& archive, const core::State& state, const core::WorldMap* map) {
    std::vector<core::Journey> journeys;
    std::vector<Journal> journals;
    archive(journeys, journals);
    const auto journeysSound = [&] {
        const bool ordered = std::ranges::adjacent_find(journeys, std::greater_equal{}, Leader) == journeys.end();
        return ordered &&
               std::ranges::all_of(journeys, [&](const core::Journey& journey) { return JourneySound(journey, state, *map); });
    };
    const auto journalsSound = [&] {
        const auto entrySound = [map](const JournalEntry& entry) {
            return IsCity(*map, entry.destination) && entry.profile < map->Profiles().size();
        };
        const auto journalSound = [&](const Journal& journal) {
            return state.DomainOf(journal.npc).has_value() && !journal.entries.empty() &&
                   journal.entries.size() <= JournalLength(state, journal.npc) &&
                   std::ranges::all_of(journal.entries, entrySound);
        };
        const bool ordered = std::ranges::adjacent_find(journals, std::greater_equal{}, &Journal::npc) == journals.end();
        return ordered && std::ranges::all_of(journals, journalSound);
    };
    std::vector<core::StableId> members;
    bool sound = !archive.Failed() && (map != nullptr || (journeys.empty() && journals.empty()));
    if (sound && map != nullptr) {
        sound = journeysSound() && journalsSound();
    }
    if (sound) {
        for (const core::Journey& journey : journeys) {
            members.insert(members.end(), journey.members.begin(), journey.members.end());
        }
        std::ranges::sort(members);
        sound = std::ranges::adjacent_find(members) == members.end(); // nobody in two journeys, or twice in one
    }
    if (sound) {
        // A month's action is Travel exactly while its NPC is away, or the month step would let a member act at home, or
        // never give a stray traveller its months back.
        sound = std::ranges::all_of(std::to_array({ ACTIVE, WORLD }), [&](core::DomainIndex domain) {
            const auto npcs = state[domain].Entities().view<const core::StableId, const Npc>();
            return std::ranges::all_of(npcs, [&](core::Entity entity) {
                const bool member = std::ranges::binary_search(members, npcs.get<const core::StableId>(entity));
                return member == (npcs.get<const Npc>(entity).action == Action::Travel);
            });
        });
    }
    if (!sound) {
        archive.Fail();
        return false;
    }
    m_journeys = std::move(journeys);
    m_journals = std::move(journals);
    m_travellers = std::unordered_set<core::StableId>(members.begin(), members.end());
    m_written.clear();
    m_merged.clear();
    return true;
}

} // namespace game
