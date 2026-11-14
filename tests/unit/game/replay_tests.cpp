// The phase 4 gate for replays (design doc 0002 §16, 0002.2), extended in phase 5 (0002.3 §9): an arena with
// every NPC system (walking active NPCs, a world of NPCs living months) recorded through the real logic, from the
// cells and changed to with F2, with the player walking and selecting, a pause, a calendar step and a save in it, plays
// back in a fresh game in another scene (a load across scenes, waiting for each) with every hash matching, the last
// one on the recording's final tick. Phase 6 chunk 4 adds a recorded fight: every ability cast, foxes dying and
// respawning, a save while the swords fly.
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/logic.hpp"
#include "core/replay.hpp"
#include "core/saves.hpp"
#include "core/scene_reports.hpp"
#include "core/tick_clock.hpp"
#include "game/host.hpp"
#include "support/manifest.hpp"
#include "support/seals.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 WORKERS = 2;

u64 NoClock() {
    return 0;
}

// A game as app::Game holds one, built as `scene`, without its clock: the tests choose the bursts. As App does, it
// reports each scene a snapshot needs resident, and each scene change's way out and way in played: at once, or `late`
// bursts after the stage first shows, as a slow main thread would.
struct Session {
    explicit Session(
        const std::filesystem::path& folder,
        core::SceneKey scene = game::ARENA_SCENE,
        const core::GameData& data = game::BuiltInData()
    )
      : saves(folder / "slot.pns", jobs)
      , replays(folder / "replay.pnr", jobs)
      , host(test::TestManifest(), scene, data) { // parentheses: MSVC C4868
        Burst(1);                                 // loading: reports the scene resident
        Burst(1);                                 // entering: reports the way in played
        Burst(1);                                 // takes that report
    }

    void Burst(u64 ticks) {
        static_cast<void>(logic.RunBurst({ .ticks = ticks }));
        core::Snapshot* published = handover.TakeState();
        volleys = published->volleys.size();
        seals = test::RaisedSeals(*published).size();
        const core::SceneChange& change = published->sceneChange;
        waited = change.change == shown.change && change.stage == shown.stage ? waited + 1 : 0;
        shown = change;
        if (waited >= late) {
            if (published->scene != reported) {
                reported = published->scene;
                handover.SendInput({ .kind = core::ActionKind::SceneResident, .scene = published->scene });
            }
            if (const std::optional<core::Action> report = reports.Answer(change)) {
                handover.SendInput(*report);
            }
        }
        handover.ReturnState(published);
    }

    void Send(core::ActionKind kind, core::StableId target = {}) {
        handover.SendInput({ .kind = kind, .target = target });
    }

    void Hold(core::Vec2 move) {
        handover.SendHeld({ .move = move });
    }

    void Cast(core::Slot slot, core::Vec2 point) {
        handover.SendInput({ .kind = core::ActionKind::Cast, .slot = slot, .point = point });
    }

    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    core::GameHandover handover{};
    // NOLINTEND(readability-redundant-member-init)
    core::Jobs jobs{ WORKERS };
    core::Saves saves;
    core::Saves replays;
    core::Replay replay{};
    game::Host host;
    core::Logic logic{ handover, host, jobs, saves, replays, replay, NoClock };
    core::SceneReports reports{};
    std::optional<core::SceneId> reported = std::nullopt;
    u32 late = 0;              // how many bursts after a stage shows it is answered
    core::SceneChange shown{}; // the stage the last snapshot showed
    u32 waited = 0;            // bursts since it first showed
    std::size_t volleys = 0;   // in flight in the last snapshot published
    std::size_t seals = 0;     // raised in play in the last snapshot published
};

// A folder of its own, removed when the test ends.
class Folder {
public:
    explicit Folder(std::string_view name)
      : m_path{ std::filesystem::temp_directory_path() / std::format("project_n_replay_tests_{}", name) } {
        std::filesystem::remove_all(m_path);
    }
    Folder(const Folder&) = delete;
    Folder& operator=(const Folder&) = delete;
    Folder(Folder&&) = delete;
    Folder& operator=(Folder&&) = delete;
    ~Folder() {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }

    [[nodiscard]] const std::filesystem::path& Path() const {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

TEST_CASE("an arena with NPCs plays its replay back with every hash matching") {
    constexpr u64 WALK = 40;
    constexpr u64 PAUSED = 10;
    constexpr u64 BURST = 7;
    constexpr u32 MAX_BURSTS = 100;
    constexpr core::Vec2 RIGHT{ .x = 1.0F, .y = 0.0F };
    constexpr core::Vec2 UP_LEFT{ .x = -0.6F, .y = -0.8F };
    const Folder folder{ "npcs" };
    core::StableId selected{};
    {
        Session recording(folder.Path(), game::CELLS_SCENE);
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
        recording.Send(core::ActionKind::CycleScene); // to the arena
        recording.Burst(BURST);                       // ends after the request: leaving, which the session reports
        recording.Burst(BURST);                       // the arena is built: loading, and the session reports it resident
        recording.Burst(BURST);                       // entering, which the session reports
        recording.Burst(BURST);                       // takes that report: the arena runs
        REQUIRE(recording.host.Player() != core::StableId{});
        recording.Hold(RIGHT);
        recording.Burst(WALK);
        selected = recording.host.Player();
        recording.Send(core::ActionKind::Select, selected);
        recording.Hold(UP_LEFT);
        recording.Burst(1); // before the pause: a frozen tick's actions do nothing
        recording.Send(core::ActionKind::Pause);
        recording.Burst(PAUSED);
        recording.Send(core::ActionKind::Resume);
        recording.Send(core::ActionKind::AdvanceCalendar);
        recording.Burst(BURST); // ends at the request
        recording.Burst(BURST); // the month step
        recording.Burst(WALK);
        recording.Hold({});
        recording.Send(core::ActionKind::Save);
        recording.Burst(BURST);
        // To a hash tick, so the recording's last state is checked.
        recording.Burst(core::REPLAY_HASH_TICKS - (recording.logic.Counters().wall % core::REPLAY_HASH_TICKS));
        REQUIRE(recording.logic.Counters().wall % core::REPLAY_HASH_TICKS == 0);
        REQUIRE(recording.host.Months() == 1);
        REQUIRE(recording.host.Selected() == selected);
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
    } // its job system finishes writing the replay

    Session playing(folder.Path()); // in the arena: playback starts in the cells, and changes to the arena
    playing.Send(core::ActionKind::PlayReplay);
    playing.Burst(1);
    REQUIRE(playing.replay.Playing());
    for (u32 bursts = 0; bursts < MAX_BURSTS && playing.replay.Playing(); ++bursts) {
        playing.Burst(core::REPLAY_HASH_TICKS);
    }
    CHECK(playing.replay.AtEnd());
    CHECK(playing.replay.HashCount() == 2);
    CHECK(playing.replay.Matched() == playing.replay.HashCount());
    CHECK(playing.host.Months() == 1);
    CHECK(playing.host.Selected() == selected);
    CHECK(playing.logic.Epoch() == 1);
}

TEST_CASE("a replay starting in another scene plays back with the same hashes") {
    constexpr u32 LATE = 5;
    constexpr u32 MAX_BURSTS = 100;
    const Folder folder{ "other_scene" };
    core::SceneId recordedIn{};
    {
        Session recording(folder.Path(), game::PAINTED_SCENE);
        recording.Burst(core::REPLAY_HASH_TICKS - 1); // starts on a hash tick, which playback checks once restored
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
        REQUIRE(recording.logic.Counters().wall == core::REPLAY_HASH_TICKS);
        recording.Burst(core::REPLAY_HASH_TICKS);
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
        recordedIn = recording.host.Scene();
    } // its job system finishes writing the replay

    for (const u32 late : { 0U, LATE }) { // the reports answered at once, and late
        CAPTURE(late);
        Session playing(folder.Path()); // in the arena
        REQUIRE(playing.host.Scene() != recordedIn);
        playing.late = late;
        playing.Send(core::ActionKind::PlayReplay);
        playing.Burst(1);
        REQUIRE(playing.replay.Playing());
        for (u32 bursts = 0; bursts < MAX_BURSTS && playing.replay.Playing(); ++bursts) {
            playing.Burst(core::REPLAY_HASH_TICKS);
        }
        CHECK(playing.replay.AtEnd());
        CHECK(playing.replay.HashCount() == 2);
        CHECK(playing.replay.Matched() == playing.replay.HashCount());
        CHECK(playing.host.Scene() == recordedIn);
        CHECK(playing.logic.Epoch() == 1);
    }
}

// The living foxes' IDs, in order: none dying.
std::vector<core::StableId> Foxes(const game::Host& host) {
    std::vector<core::StableId> foxes;
    const core::Registry& entities = host.State()[game::ACTIVE].Entities();
    for (auto [entity, creature, stable] : entities.view<const game::Creature, const core::StableId>().each()) {
        if (creature.kind == game::CreatureKind::Fox && !entities.all_of<game::Dying>(entity)) {
            foxes.push_back(stable);
        }
    }
    std::ranges::sort(foxes);
    return foxes;
}

// Where `entity`'s foot is now.
core::Vec2 FootOf(const game::Host& host, core::StableId entity) {
    return host.State()[game::ACTIVE].Entities().get<game::Body>(host.State()[game::ACTIVE].Find(entity)).current;
}

TEST_CASE("a recorded fight replays with every hash matching") {
    // Every ability cast at the foxes through the real logic, foxes weak enough to die and respawn, and a save while the
    // swords fly: the fight plays back in a fresh game with every hash matching, the last on the final tick.
    constexpr i64 WEAK_FOX = 300; // below one lance's hit
    constexpr u64 LANCE_COOLING = 31;
    constexpr u64 BOLT_COOLING = 16;
    constexpr u64 DASHING = 20;
    constexpr u64 TO_MID_VOLLEY = 60;
    constexpr u64 TO_RESPAWNS = 600; // past every death's dissolve, respawn and easing in
    constexpr i64 QI_LEFT = 20;      // 100, less two lances (25 each), plus three bolts (10 each), less the swords (60)
    constexpr core::Vec2 DASH_TO{ .x = 400.0F, .y = 0.0F };
    constexpr u32 MAX_BURSTS = 100;
    constexpr std::size_t HASHES = 4; // the hashes this recording holds, the last on its final tick
    core::GameData data = game::BuiltInData();
    data.fox.health = WEAK_FOX;
    data.abilities.qiStart = data.abilities.qiMax;
    const Folder folder{ "fight" };
    {
        Session recording(folder.Path(), game::ARENA_SCENE, data);
        const std::vector<core::StableId> first = Foxes(recording.host);
        REQUIRE(first.size() >= 3);
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
        // Each at a fox still living: the lances kill theirs at once.
        for (u32 lance = 0; lance < 2; ++lance) {
            recording.Cast(core::Slot::Spender, FootOf(recording.host, Foxes(recording.host).front()));
            recording.Burst(LANCE_COOLING);
        }
        for (u32 bolt = 0; bolt < 3; ++bolt) {
            recording.Cast(core::Slot::Generator, FootOf(recording.host, Foxes(recording.host).front()));
            recording.Burst(BOLT_COOLING);
        }
        const core::Vec2 dashedFrom = FootOf(recording.host, recording.host.Player());
        recording.Cast(core::Slot::Movement, dashedFrom + DASH_TO);
        recording.Burst(DASHING);
        const f32 dashed = FootOf(recording.host, recording.host.Player()).x - dashedFrom.x; // straight right
        CHECK(dashed == doctest::Approx(data.abilities.Of(core::AbilityKind::Dash).distance));
        recording.Cast(core::Slot::Special1, FootOf(recording.host, Foxes(recording.host).back()));
        recording.Burst(TO_MID_VOLLEY);
        REQUIRE(recording.volleys > 0); // the swords fly
        REQUIRE(recording.seals > 0);   // over their seal, which the hashes then cover
        recording.Send(core::ActionKind::Save);
        recording.Burst(TO_RESPAWNS);
        recording.Burst(core::REPLAY_HASH_TICKS - (recording.logic.Counters().wall % core::REPLAY_HASH_TICKS));
        REQUIRE(recording.logic.Counters().wall % core::REPLAY_HASH_TICKS == 0);
        const core::Registry& entities = recording.host.State()[game::ACTIVE].Entities();
        CHECK(entities.get<game::Caster>(recording.host.State()[game::ACTIVE].Find(recording.host.Player())).qi == QI_LEFT);
        const std::vector<core::StableId> now = Foxes(recording.host);
        CHECK(std::ranges::any_of(now, [&first](core::StableId fox) { return !std::ranges::contains(first, fox); }));
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
    } // its job system finishes writing the replay

    Session playing(folder.Path(), game::ARENA_SCENE, data);
    playing.Send(core::ActionKind::PlayReplay);
    playing.Burst(1);
    REQUIRE(playing.replay.Playing());
    for (u32 bursts = 0; bursts < MAX_BURSTS && playing.replay.Playing(); ++bursts) {
        playing.Burst(core::REPLAY_HASH_TICKS);
    }
    CHECK(playing.replay.AtEnd());
    CHECK(playing.replay.HashCount() == HASHES);
    CHECK(playing.replay.Matched() == playing.replay.HashCount());
}

TEST_CASE("a replay recorded through a portal plays back with the same hashes") {
    // The cultivator walks from its start onto the arena's portal through the real logic, which changes the scene to the
    // stress arena as F2 would: the walk, the step and the scene it leads to play back with every hash matching.
    constexpr u64 TO_THE_PORTAL = 600; // more ticks than the walk takes: the step onto it ends the burst
    constexpr u64 BURST = 7;
    constexpr u32 MAX_BURSTS = 100;
    constexpr std::size_t HASHES = 5; // four on the walk, the last in the stress arena on the final tick
    const Folder folder{ "portal" };
    {
        Session recording(folder.Path());
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
        const std::optional<core::Vec2> portal = game::SceneOf(game::ARENA_SCENE).Traits().portal;
        REQUIRE(portal.has_value());
        const core::Vec2 away = portal.value_or(core::Vec2{}) - FootOf(recording.host, recording.host.Player());
        recording.Hold(away * (1.0F / std::hypot(away.x, away.y)));
        const u64 setOff = recording.logic.Counters().wall;
        recording.Burst(TO_THE_PORTAL); // ends after the step onto it: leaving, which the session reports
        REQUIRE(recording.logic.Counters().wall < setOff + TO_THE_PORTAL);
        recording.Hold({});
        recording.Burst(BURST); // the stress arena is built: loading, and the session reports it resident
        recording.Burst(BURST); // entering, which the session reports
        recording.Burst(BURST); // takes that report: the stress arena runs
        REQUIRE(recording.host.Current().key == game::STRESS_SCENE);
        // To a hash tick, so the recording's last state is checked.
        recording.Burst(core::REPLAY_HASH_TICKS - (recording.logic.Counters().wall % core::REPLAY_HASH_TICKS));
        REQUIRE(recording.logic.Counters().wall % core::REPLAY_HASH_TICKS == 0);
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
    } // its job system finishes writing the replay

    Session playing(folder.Path());
    playing.Send(core::ActionKind::PlayReplay);
    playing.Burst(1);
    REQUIRE(playing.replay.Playing());
    for (u32 bursts = 0; bursts < MAX_BURSTS && playing.replay.Playing(); ++bursts) {
        playing.Burst(core::REPLAY_HASH_TICKS);
    }
    CHECK(playing.replay.AtEnd());
    CHECK(playing.replay.HashCount() == HASHES);
    CHECK(playing.replay.Matched() == playing.replay.HashCount());
    CHECK(playing.host.Current().key == game::STRESS_SCENE);
}

} // namespace
