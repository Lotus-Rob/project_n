#include "game/travel.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "game/npcs.hpp"

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

/// Whether `leg`, read from a save, can be placed on `map`: its shape and profile exist, it starts from a finite point,
/// its tiles are on the map, one arrive tick each from 0 and rising, and its end tick fits.
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

i64 PartStartTick(const core::JourneyPart& part) {
    return part.stay ? part.rest.startTick : part.leg.startTick;
}

i64 PartEndTick(const core::JourneyPart& part) {
    return part.stay ? part.rest.endTick : core::EndTick(part.leg);
}

/// Whether `journey`, read from a save, suits `state` and `map`: members, all world NPCs, between two cities, and parts,
/// each sound and starting on the tick the one before ends.
bool JourneySound(const core::Journey& journey, const core::State& state, const core::WorldMap& map) {
    const auto gap = [](const core::JourneyPart& before, const core::JourneyPart& after) {
        return PartStartTick(after) != PartEndTick(before);
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

} // namespace

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
