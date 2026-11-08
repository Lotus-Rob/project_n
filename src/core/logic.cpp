#include "core/logic.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/math.hpp"
#include "core/profile.hpp"

namespace core {

namespace {

constexpr u32 SAVE_FORMAT = 1; // a save's start: this, the tick counters, then the simulation's state

constexpr u32 Bit(ActionKind kind) {
    return 1U << std::to_underlying(kind);
}

} // namespace

Simulation::~Simulation() = default;

bool Logic::RunBurst(const TickBurst& burst, const std::stop_token& stop) {
    PN_PROFILE_ZONE();
    TickEnd end = TickEnd::Continue;
    for (u64 tick = 0; tick < burst.ticks && end == TickEnd::Continue; ++tick) {
        end = RunTick(stop);
        CheckReplay();
        PN_ASSERT(math::FloatingPointEnvironmentIsSet(), "a library changed the floating-point environment");
    }
    if (m_calendar == Calendar::Requested) {
        m_calendar = Calendar::Published;
    }
    // Field by field: assigning a whole Snapshot would free the instance and batch buffers' capacity.
    Snapshot& snapshot = m_handover->Writing();
    snapshot.wallTick = m_counters.wall;
    snapshot.simulationTick = m_counters.simulation;
    snapshot.epoch = m_epoch;
    snapshot.scheduledNs = burst.scheduledNs;
    snapshot.advanced = m_advanced;
    snapshot.transition = m_calendar == Calendar::Published;
    snapshot.instances.clear();
    snapshot.batches.clear();
    snapshot.ids.clear();
    snapshot.follow.reset();
    snapshot.selected.reset();
    m_simulation->Publish(snapshot, *m_jobs);
    snapshot.publishedNs = m_now();
    m_handover->Publish(m_events);
    return end == TickEnd::Stall;
}

Logic::TickEnd Logic::RunTick(const std::stop_token& stop) {
    PN_PROFILE_ZONE();
    m_advanced = false;
    if (m_calendar == Calendar::Published) {
        m_counters.Count(true, true);
        m_simulation->AdvanceCalendar(m_counters, *m_jobs, stop);
        m_calendar = Calendar::None;
        return TickEnd::Stall;
    }
    const ActionSet asked = TakeInput();
    if ((asked & Bit(ActionKind::ReloadData)) != 0) {
        ReloadData(); // first: whatever else this tick does, it does under the new data
    }
    if ((asked & Bit(ActionKind::PlayReplay)) != 0) {
        if (!TogglePlayback()) {
            m_counters.Count(true, false);
        }
        return TickEnd::Stall;
    }
    if ((asked & Bit(ActionKind::Load)) != 0) {
        if (!Load()) {
            m_counters.Count(true, false);
        }
        return TickEnd::Stall;
    }
    if ((asked & (Bit(ActionKind::ToggleRecording) | Bit(ActionKind::Save))) != 0) {
        m_counters.Count(true, false);
        if ((asked & Bit(ActionKind::Save)) != 0) {
            Save(); // first: a recording starting here begins after it
        }
        if ((asked & Bit(ActionKind::ToggleRecording)) != 0) {
            ToggleRecording();
        }
        return TickEnd::Stall;
    }
    if ((asked & Bit(ActionKind::ReloadData)) != 0) {
        m_counters.Count(true, false);
        return TickEnd::EndBurst;
    }
    m_advanced = !m_paused && m_calendar == Calendar::None;
    m_counters.Count(!m_advanced, false);
    if (m_advanced) {
        m_simulation->Tick(m_counters, { .actions = m_input, .held = m_held });
    }
    return m_calendar == Calendar::Requested ? TickEnd::EndBurst : TickEnd::Continue;
}

Logic::ActionSet Logic::TakeInput() {
    PN_PROFILE_ZONE();
    m_handover->TakeInput(m_input);
    m_held = m_handover->ReadHeld();
    for (const Action& action : m_input) {
        if (action.kind == ActionKind::Pause || action.kind == ActionKind::Resume) {
            m_windowPaused = action.kind == ActionKind::Pause;
        }
    }
    if (m_replay->Playing()) {
        // Live input is dropped while a replay plays, but for new game data, which stops it, and the request to stop it.
        if (std::ranges::any_of(m_input, [](const Action& action) { return action.kind == ActionKind::ReloadData; })) {
            StopPlaying("the game data changed");
        }
        else if (std::ranges::any_of(m_input, [](const Action& action) { return action.kind == ActionKind::PlayReplay; })) {
            return Bit(ActionKind::PlayReplay);
        }
        else if (m_replay->AtEnd()) {
            StopPlaying("it finished");
        }
        else {
            const TickInput played = m_replay->Next();
            m_input.assign(played.actions.begin(), played.actions.end());
            m_held = played.held;
        }
    }
    ActionSet asked = 0;
    for (const Action& action : m_input) {
        asked |= Bit(action.kind);
        if (action.kind == ActionKind::Pause || action.kind == ActionKind::Resume) {
            m_paused = action.kind == ActionKind::Pause; // the last one wins
        }
    }
    if ((asked & Bit(ActionKind::AdvanceCalendar)) != 0) {
        m_calendar = Calendar::Requested;
    }
    constexpr ActionSet ENDS_TIMELINE =
        Bit(ActionKind::ToggleRecording) | Bit(ActionKind::PlayReplay) | Bit(ActionKind::Load) | Bit(ActionKind::ReloadData);
    if (m_replay->Recording() && (asked & ENDS_TIMELINE) == 0) {
        m_replay->Record({ .actions = m_input, .held = m_held });
    }
    return asked;
}

u64 Logic::Hash() const {
    Hasher hasher;
    hasher(m_counters.wall, m_counters.simulation, m_counters.state, m_paused, m_calendar, m_simulation->StateHash());
    return hasher.Value();
}

void Logic::CheckReplay() {
    PN_PROFILE_ZONE();
    if (m_counters.wall % REPLAY_HASH_TICKS != 0) {
        return;
    }
    if (m_replay->Recording()) {
        m_replay->RecordHash(m_counters.wall, Hash());
    }
    else if (m_replay->Playing() && m_replay->CheckHash(m_counters.wall, Hash()) == HashCheck::Diverged) {
        PN_LOG_ERROR("the replay diverged from its recording at wall tick {}", m_counters.wall);
        StopPlaying("it diverged");
    }
}

std::vector<std::byte> Logic::Serialise(bool withLogicState) {
    std::vector<std::byte> state;
    Writer archive{ state };
    if (withLogicState) {
        archive(m_paused, m_calendar);
    }
    archive(SAVE_FORMAT, m_counters.wall, m_counters.simulation, m_counters.state);
    m_simulation->Save(archive);
    return state;
}

bool Logic::Restore(std::span<const std::byte> state, bool withLogicState) {
    Reader archive{ state };
    bool paused = m_paused;
    Calendar calendar = Calendar::None;
    if (withLogicState) {
        archive(paused, calendar);
        if (calendar > Calendar::Published) {
            archive.Fail();
        }
    }
    u32 format = 0;
    TickCounters counters;
    archive(format, counters.wall, counters.simulation, counters.state);
    if (archive.Failed() || format != SAVE_FORMAT || !m_simulation->Load(archive)) {
        return false;
    }
    m_counters = counters;
    m_paused = paused;
    m_calendar = calendar;
    ++m_epoch;
    return true;
}

void Logic::Save() {
    PN_PROFILE_ZONE();
    std::vector<std::byte> state = Serialise(false);
    if (m_replay->Playing()) {
        PN_LOG_DEBUG("a replayed save at tick {}: the state is sorted, as it was, and no file written", m_counters.wall);
        return;
    }
    PN_LOG_INFO("saving the game at tick {}: {} bytes of state", m_counters.wall, state.size());
    m_saves->Write(std::move(state));
}

bool Logic::Load() {
    PN_PROFILE_ZONE();
    if (m_replay->Recording()) {
        FinishRecording(); // a load starts a new timeline
    }
    PN_ASSERT(!m_replay->Playing(), "a load during playback: live input is dropped, and replays hold no loads");
    const std::expected<std::vector<std::byte>, Error> state = m_saves->Read();
    if (!state) {
        PN_LOG_ERROR("not loaded: {}", state.error().message);
        return false;
    }
    if (!Restore(*state, false)) {
        PN_LOG_ERROR("not loaded: {} is malformed or from another version of the game", m_saves->Slot().string());
        return false;
    }
    PN_LOG_INFO("loaded the game from {} at tick {}; timeline {} begins", m_saves->Slot().string(), m_counters.wall, m_epoch);
    return true;
}

void Logic::ToggleRecording() {
    PN_PROFILE_ZONE();
    if (m_replay->Recording()) {
        FinishRecording();
        return;
    }
    m_replay->StartRecording(Serialise(true), m_simulation->DataHash());
    PN_LOG_INFO("recording a replay from tick {}", m_counters.wall);
}

void Logic::FinishRecording() {
    std::vector<std::byte> replay = m_replay->FinishRecording();
    PN_LOG_INFO("finished recording a replay at tick {}: {} bytes", m_counters.wall, replay.size());
    m_replays->Write(std::move(replay));
}

bool Logic::TogglePlayback() {
    PN_PROFILE_ZONE();
    if (m_replay->Playing()) {
        StopPlaying("the player stopped it");
        return false;
    }
    if (m_replay->Recording()) {
        FinishRecording(); // the replay just recorded is the one to play
    }
    std::expected<std::vector<std::byte>, Error> replay = m_replays->Read();
    if (!replay) {
        PN_LOG_ERROR("no replay played: {}", replay.error().message);
        return false;
    }
    const std::expected<std::span<const std::byte>, Error> start =
        m_replay->StartPlaying(std::move(*replay), m_simulation->DataHash());
    if (!start) {
        PN_LOG_ERROR("no replay played: {}", start.error().message);
        return false;
    }
    if (!Restore(*start, true)) {
        m_replay->StopPlaying();
        PN_LOG_ERROR("no replay played: its start state is malformed");
        return false;
    }
    PN_LOG_INFO("playing the replay from tick {}; timeline {} begins", m_counters.wall, m_epoch);
    return true;
}

void Logic::ReloadData() {
    PN_PROFILE_ZONE();
    const auto last = std::ranges::find_last(m_input, ActionKind::ReloadData, &Action::kind).begin();
    PN_ASSERT(last != m_input.end() && last->data != nullptr, "ReloadData sent without its data");
    if (m_replay->Recording()) {
        PN_LOG_INFO("the game data changed: the replay being recorded ends here");
        FinishRecording();
    }
    m_simulation->SetData(*last->data);
    PN_LOG_INFO("new game data at tick {}", m_counters.wall);
}

void Logic::StopPlaying(std::string_view why) {
    PN_LOG_INFO("replay stopped at tick {} because {}; {} hash checks matched", m_counters.wall, why, m_replay->Matched());
    m_replay->StopPlaying();
    m_paused = m_windowPaused; // the window's focus changed, perhaps, while the replay's input ruled
}

} // namespace core
