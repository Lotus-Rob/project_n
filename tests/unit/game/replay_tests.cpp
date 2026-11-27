// The replay gate: an arena with
// every NPC system (walking active NPCs, a world of NPCs living months) recorded through the real logic, from the
// cells and changed to with F2, with the player walking and selecting, a pause, a calendar step and a save in it, plays
// back in a fresh game in another scene (a load across scenes, waiting for each) with every hash matching, the last
// one on the recording's final tick. A recorded fight is replayed the same way: every ability cast, foxes dying and
// respawning, a save while the swords fly. So is the world scene on the shipped map, walked across a
// month step, and the cultivator walked along routes there, exploring the same tiles; and a recording that starts from a
// world a month on. A skipped long run ticks two games through an hour of seeded walk-tos, one on 1 worker and one on 4,
// and compares their hashes as they go.
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/log.hpp"
#include "core/logic.hpp"
#include "core/random.hpp"
#include "core/replay.hpp"
#include "core/saves.hpp"
#include "core/scene_reports.hpp"
#include "core/tick_clock.hpp"
#include "core/world_map.hpp"
#include "game/host.hpp"
#include "support/fog.hpp"
#include "support/host.hpp"
#include "support/manifest.hpp"
#include "support/saves.hpp"
#include "support/seals.hpp"
#include "support/world.hpp"

#include <doctest/doctest.h>

namespace {

using test::ExploredCount;
using test::ExploredOf;
using test::FootOf;
using test::LivingFoxes;

constexpr u32 WORKERS = 2;

u64 NoClock() {
    return 0;
}

// Has `host` load and enter `start`, a host save, as the logic does at a load's swap; returns whether it loaded.
bool StartFrom(game::Host& host, std::span<const std::byte> start) {
    core::Reader reader{ start };
    if (!host.Load(reader, 0)) {
        return false;
    }
    host.EnterScene();
    return true;
}

// A game as app::Game holds one, built as `scene`, without its clock: the tests choose the bursts. As App does, it
// reports each scene a snapshot needs resident, and each scene change's way out and way in played: at once, or `late`
// bursts after the stage first shows, as a slow main thread would.
struct Session {
    explicit Session(
        const std::filesystem::path& folder,
        core::SceneKey scene = game::ARENA_SCENE,
        const core::GameData& data = test::SmallData(),
        u32 workers = WORKERS
    )
      : jobs{ workers }
      , saves(folder / "slot.pns", jobs)
      , replays(folder / "replay.pnr", jobs)
      , host(test::TestManifest(), scene, data) { // parentheses: MSVC C4868
        Start();
    }

    // A game whose host starts from `start`, a host save it loads and enters before the logic is built (on `data`, which
    // the save was made for), as if its scene had been built so: a state no run reaches cheaply.
    Session(const std::filesystem::path& folder, std::span<const std::byte> start, core::GameData data = test::SmallData())
      : saves(folder / "slot.pns", jobs)
      , replays(folder / "replay.pnr", jobs)
      , host(test::TestManifest(), game::ARENA_SCENE, std::move(data)) // parentheses: MSVC C4868
      , started(StartFrom(host, start)) {
        Start();
    }

    // Runs the game's start: its scene loaded, entered and running.
    void Start() {
        Burst(1); // loading: reports the scene resident
        Burst(1); // entering: reports the way in played
        Burst(1); // takes that report
    }

    void Burst(u64 ticks) {
        static_cast<void>(logic.RunBurst({ .ticks = ticks }));
        core::Snapshot* published = handover.TakeState();
        volleys = published->volleys.size();
        transition = published->transition;
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

    void WalkTo(core::Vec2 point) {
        handover.SendInput({ .kind = core::ActionKind::WalkTo, .point = point });
    }

    core::GameHandover handover;
    core::Jobs jobs{ WORKERS };
    core::Saves saves;
    core::Saves replays;
    core::Replay replay{};
    game::Host host;
    bool started = false; // whether the host loaded the start state it was given
    core::Logic logic{ handover, host, jobs, saves, replays, replay, NoClock };
    core::SceneReports reports{};
    std::optional<core::SceneId> reported = std::nullopt;
    u32 late = 0;              // how many bursts after a stage shows it is answered
    core::SceneChange shown{}; // the stage the last snapshot showed
    u32 waited = 0;            // bursts since it first showed
    std::size_t volleys = 0;   // in flight in the last snapshot published
    std::size_t seals = 0;     // raised in play in the last snapshot published
    bool transition = false;   // whether the last snapshot published showed a calendar step's transition
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
        const std::vector<core::StableId> first = LivingFoxes(recording.host);
        REQUIRE(first.size() >= 3);
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
        // Each at a fox still living: the lances kill theirs at once.
        for (u32 lance = 0; lance < 2; ++lance) {
            recording.Cast(core::Slot::Spender, FootOf(recording.host, LivingFoxes(recording.host).front()));
            recording.Burst(LANCE_COOLING);
        }
        for (u32 bolt = 0; bolt < 3; ++bolt) {
            recording.Cast(core::Slot::Generator, FootOf(recording.host, LivingFoxes(recording.host).front()));
            recording.Burst(BOLT_COOLING);
        }
        const core::Vec2 dashedFrom = FootOf(recording.host, recording.host.Player());
        recording.Cast(core::Slot::Movement, dashedFrom + DASH_TO);
        recording.Burst(DASHING);
        const f32 dashed = FootOf(recording.host, recording.host.Player()).x - dashedFrom.x; // straight right
        CHECK(dashed == doctest::Approx(data.abilities.Of(core::AbilityKind::Dash).distance));
        recording.Cast(core::Slot::Special1, FootOf(recording.host, LivingFoxes(recording.host).back()));
        recording.Burst(TO_MID_VOLLEY);
        REQUIRE(recording.volleys > 0); // the swords fly
        REQUIRE(recording.seals > 0);   // over their seal, which the hashes then cover
        recording.Send(core::ActionKind::Save);
        recording.Burst(TO_RESPAWNS);
        recording.Burst(core::REPLAY_HASH_TICKS - (recording.logic.Counters().wall % core::REPLAY_HASH_TICKS));
        REQUIRE(recording.logic.Counters().wall % core::REPLAY_HASH_TICKS == 0);
        const core::Registry& entities = recording.host.State()[game::ACTIVE].Entities();
        CHECK(entities.get<game::Caster>(recording.host.State()[game::ACTIVE].Find(recording.host.Player())).qi == QI_LEFT);
        const std::vector<core::StableId> now = LivingFoxes(recording.host);
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

TEST_CASE("a replay on the world scene plays back with the same hashes") {
    // On the shipped map with its cities' 50,000 NPCs: the cultivator walks east from the start city, the world is paused, a
    // month passes (M while paused), the world resumes and the cultivator walks on, across more than one hash window; a fresh
    // game on the same map plays it back with every hash matching.
    constexpr u64 WALK = 40;
    constexpr u64 BURST = 7;
    constexpr u32 MAX_BURSTS = 100;
    constexpr std::size_t HASHES = 3; // the last on the recording's final tick
    constexpr core::Vec2 RIGHT{ .x = 1.0F, .y = 0.0F };
    // The shipped data stays here: the start city's full sight clear of the map's edges, and the cities' crowd, are the
    // shipped map's, which the small map's corner start and 100 people cannot stand in for.
    const core::GameData data = test::ShippedData();
    const Folder folder{ "world" };
    {
        Session recording(folder.Path(), game::WORLD_SCENE, data);
        REQUIRE(recording.host.Player() != core::StableId{});
        // A new game has explored the whole diamond of its sight round the start city, which lies clear of the map's edges.
        CHECK(ExploredCount(recording.host.ExploredTiles()) == 25);
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
        const core::Vec2 start = FootOf(recording.host, recording.host.Player());
        recording.Hold(RIGHT);
        recording.Burst(WALK);
        recording.Send(core::ActionKind::ToggleWorldPause);
        recording.Burst(1);
        REQUIRE(recording.host.WorldPaused());
        recording.Send(core::ActionKind::AdvanceCalendar);
        recording.Burst(BURST); // ends at the request
        recording.Burst(BURST); // the month step
        REQUIRE(recording.host.WorldTicks() == game::TICKS_PER_MONTH);
        recording.Send(core::ActionKind::ToggleWorldPause);
        recording.Burst(WALK);
        recording.Hold({});
        // To a hash tick past the first window, so the recording's last state is checked.
        recording.Burst(core::REPLAY_HASH_TICKS);
        recording.Burst(core::REPLAY_HASH_TICKS - (recording.logic.Counters().wall % core::REPLAY_HASH_TICKS));
        REQUIRE(recording.logic.Counters().wall % core::REPLAY_HASH_TICKS == 0);
        REQUIRE(recording.host.Months() == 1);
        REQUIRE(FootOf(recording.host, recording.host.Player()).x > start.x); // it walked
        REQUIRE(recording.host.WorldTicks() > game::TICKS_PER_MONTH); // it ran again once resumed (hashed: the hashes check it)
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
    } // its job system finishes writing the replay

    Session playing(folder.Path(), game::WORLD_SCENE, data);
    playing.Send(core::ActionKind::PlayReplay);
    playing.Burst(1);
    REQUIRE(playing.replay.Playing());
    for (u32 bursts = 0; bursts < MAX_BURSTS && playing.replay.Playing(); ++bursts) {
        playing.Burst(core::REPLAY_HASH_TICKS);
    }
    CHECK(playing.replay.AtEnd());
    CHECK(playing.replay.HashCount() == HASHES);
    CHECK(playing.replay.Matched() == playing.replay.HashCount());
    CHECK(playing.host.Months() == 1);
    CHECK(playing.host.Current().key == game::WORLD_SCENE);
}

TEST_CASE("a replay recorded from a delta save plays back") {
    // Recorded on the small test map, its travellers out, from a world a month on, so the replay starts from a save holding the
    // world's difference from the one its seed makes: the cultivator walks, F2 goes to the arena, where a month of its own
    // scratch world passes, and a save is made there; a fresh game in the arena plays it back with every hash matching.
    constexpr u64 WALK = 40;
    constexpr u64 BURST = 7;
    constexpr u32 MAX_BURSTS = 100;
    constexpr std::size_t HASHES = 2; // the last on the recording's final tick
    constexpr core::Vec2 RIGHT{ .x = 1.0F, .y = 0.0F };
    const core::GameData data = test::TravellingSmallData(); // its travellers out, so their journeys differ too
    const Folder folder{ "delta" };
    {
        Session recording(folder.Path(), game::WORLD_SCENE, data);
        recording.Send(core::ActionKind::AdvanceCalendar);
        recording.Burst(BURST); // ends at the request
        recording.Burst(BURST); // the month step
        REQUIRE(recording.host.Months() == 1);
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
        recording.Hold(RIGHT);
        recording.Burst(WALK);
        recording.Hold({});
        recording.Send(core::ActionKind::CycleScene); // to the arena
        recording.Burst(BURST);                       // ends after the request: leaving, which the session reports
        recording.Burst(BURST);                       // the arena is built: loading, and the session reports it resident
        recording.Burst(BURST);                       // entering, which the session reports
        recording.Burst(BURST);                       // takes that report: the arena runs
        REQUIRE(recording.host.Current().key == game::ARENA_SCENE);
        recording.Send(core::ActionKind::AdvanceCalendar);
        recording.Burst(BURST);
        recording.Burst(BURST);
        REQUIRE(recording.host.Months() == 1); // the arena's own
        recording.Send(core::ActionKind::Save);
        recording.Burst(BURST);
        // To a hash tick past the next window, so the arena after the save and the recording's last state are checked.
        recording.Burst(core::REPLAY_HASH_TICKS);
        recording.Burst(core::REPLAY_HASH_TICKS - (recording.logic.Counters().wall % core::REPLAY_HASH_TICKS));
        REQUIRE(recording.logic.Counters().wall % core::REPLAY_HASH_TICKS == 0);
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
    CHECK(playing.host.Months() == 1);
    CHECK(playing.host.Current().key == game::ARENA_SCENE);
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

TEST_CASE("a world clock reaching its month's end runs the month step through the logic") {
    // The two sides of the automatic step: the host stops its clock at the month's end, and the logic asks for the step
    // after that tick, publishing the transition, then runs it on the next.
    constexpr u64 BURST = 3;
    const Folder folder{ "month_end" };
    const core::GameData data = test::SmallData();
    const std::vector<std::byte> start = test::WorldSaveAt({ .ticks = game::TICKS_PER_MONTH - 1, .months = 0 }, &data);
    Session session(folder.Path(), start, data);
    REQUIRE(session.started);
    REQUIRE(session.host.Current().key == game::WORLD_SCENE);

    session.Burst(BURST); // ends after the month's last tick
    CHECK(session.transition);
    CHECK(session.host.WorldTicks() == game::TICKS_PER_MONTH);
    CHECK(session.host.Months() == 0);
    CHECK(session.logic.Counters().simulation == 1);

    session.Burst(BURST); // the step, on a frozen tick
    CHECK(session.host.Months() == 1);
    CHECK_FALSE(session.transition);
    CHECK(session.host.WorldTicks() == game::TICKS_PER_MONTH);

    session.Burst(BURST);
    CHECK(session.host.WorldTicks() == game::TICKS_PER_MONTH + BURST); // the clock runs again
    CHECK(session.host.Months() == 1);
}

TEST_CASE("a replay with walk-tos on the world map plays back with the same hashes") {
    // The cultivator walks a route east, a second walk-to turns it mid-route, held movement cancels that, and a third sends
    // it back to the start city: a fresh game plays it back with every hash matching, routes and all.
    constexpr u64 WALK = 40;
    constexpr u32 MAX_BURSTS = 100;
    constexpr std::size_t HASHES = 3; // the last on the recording's final tick
    constexpr core::Vec2 RIGHT{ .x = 1.0F, .y = 0.0F };
    // Where the walk-tos go, in tiles from the start city's.
    constexpr core::Vec2 EAST{ .x = 4.0F, .y = 0.0F };
    constexpr core::Vec2 SOUTH_EAST{ .x = 2.0F, .y = 2.0F };
    const core::GameData data = test::SmallData();
    const f32 tile = data.world->TileSize();
    const core::Vec2 start = data.world->StartCentre();
    const std::vector<std::byte> begin = test::LoneCultivatorSave(data, data.world->StartCentre());
    // The destinations barely fit the small map: a smaller one fails here, not as a walk-to that sets no route.
    const u32 startTile = data.world->TileAt(start);
    REQUIRE((startTile % data.world->Columns()) + static_cast<u32>(EAST.x) < data.world->Columns());
    REQUIRE((startTile / data.world->Columns()) + static_cast<u32>(SOUTH_EAST.y) < data.world->Rows());
    const Folder folder{ "walk_to" };
    {
        Session recording(folder.Path(), begin, data);
        REQUIRE(recording.started);
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
        recording.WalkTo(start + (EAST * tile));
        recording.Burst(WALK);
        REQUIRE(recording.host.PlayerRoute().has_value());
        recording.WalkTo(start + (SOUTH_EAST * tile));
        recording.Burst(WALK);
        REQUIRE(recording.host.PlayerRoute().has_value());
        recording.Hold(RIGHT);
        recording.Burst(1);
        recording.Hold({});
        REQUIRE_FALSE(recording.host.PlayerRoute().has_value());
        recording.WalkTo(start);
        recording.Burst(WALK);
        REQUIRE(recording.host.PlayerRoute().has_value());
        // To a hash tick, so the recording's last state is checked.
        recording.Burst(core::REPLAY_HASH_TICKS - (recording.logic.Counters().wall % core::REPLAY_HASH_TICKS));
        REQUIRE(recording.logic.Counters().wall % core::REPLAY_HASH_TICKS == 0);
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
    } // its job system finishes writing the replay

    Session playing(folder.Path(), begin, data);
    REQUIRE(playing.started);
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

TEST_CASE("a replay recorded across a month's jump on a route plays back exploring the same tiles") {
    // A walk-to east to a tile past the start city's sight, then a month step (M while paused) that carries the cultivator
    // to the route's end in one jump: only the route's own exploring uncovers the tiles it passed, so (2, 2), within
    // sight of the route's tiles between its ends but of neither end, is explored; a fresh game plays it back with every
    // hash matching and the same tiles explored.
    constexpr u64 BURST = 7;
    constexpr u32 MAX_BURSTS = 100;
    constexpr core::Vec2 EAST{ .x = 4.0F, .y = 0.0F }; // in tiles from the start city's
    const core::GameData data = test::SmallData();
    const core::WorldMap& map = *data.world;
    const f32 tile = map.TileSize();
    const core::Vec2 start = map.StartCentre();
    const core::Vec2 target = start + (EAST * tile);
    const std::vector<std::byte> begin = test::LoneCultivatorSave(data, data.world->StartCentre());
    const std::vector<u32> startTile{ map.TileAt(start) };
    const u32 endTile = map.TileAt(target);
    const u32 passed =
        map.TileIndex({ .column = 2, .row = 2 }); // beside the route's column-2 tile, out of the start's and the end's sight
    const std::vector<u32> endTiles{ endTile };
    REQUIRE(map.TileAt(target) == map.TileIndex({ .column = 4, .row = 0 }));
    const std::vector<u64> seenFromStart = test::Diamonds(map, startTile, data.cultivator.sight);
    const std::vector<u64> seenFromEnd = test::Diamonds(map, endTiles, data.cultivator.sight);
    REQUIRE_FALSE(core::IsExplored(seenFromStart, endTile));
    REQUIRE_FALSE(core::IsExplored(seenFromStart, passed));
    REQUIRE_FALSE(core::IsExplored(seenFromEnd, passed));
    const Folder folder{ "exploring" };
    std::vector<u64> explored;
    {
        Session recording(folder.Path(), begin, data);
        REQUIRE(recording.started);
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
        const std::vector<u64> before = ExploredOf(recording.host);
        recording.WalkTo(target);
        recording.Burst(1);
        REQUIRE(recording.host.PlayerRoute().has_value());
        recording.Send(core::ActionKind::ToggleWorldPause);
        recording.Burst(1);
        recording.Send(core::ActionKind::AdvanceCalendar);
        recording.Burst(BURST); // ends at the request
        recording.Burst(BURST); // the month step
        REQUIRE_FALSE(recording.host.PlayerRoute().has_value());
        // To a hash tick, so the recording's last state is checked.
        recording.Burst(core::REPLAY_HASH_TICKS - (recording.logic.Counters().wall % core::REPLAY_HASH_TICKS));
        REQUIRE(recording.logic.Counters().wall % core::REPLAY_HASH_TICKS == 0);
        explored = ExploredOf(recording.host);
        REQUIRE(explored != before); // the walk uncovered tiles
        REQUIRE(core::IsExplored(explored, endTile));
        REQUIRE(core::IsExplored(explored, passed));
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
    } // its job system finishes writing the replay

    Session playing(folder.Path(), begin, data);
    REQUIRE(playing.started);
    playing.Send(core::ActionKind::PlayReplay);
    playing.Burst(1);
    REQUIRE(playing.replay.Playing());
    for (u32 bursts = 0; bursts < MAX_BURSTS && playing.replay.Playing(); ++bursts) {
        playing.Burst(core::REPLAY_HASH_TICKS);
    }
    CHECK(playing.replay.AtEnd());
    CHECK(playing.replay.HashCount() > 0);
    CHECK(playing.replay.Matched() == playing.replay.HashCount());
    CHECK(ExploredOf(playing.host) == explored);
}

TEST_CASE("the world ticked through an hour of walk-tos is the same on 1 and 4 workers" * doctest::skip()) {
    // Two games on the shipped map, one on 1 worker and one on 4, are sent the same walk-tos at the same wall ticks and
    // ticked through the logic, month steps and all, comparing both hashes after every burst so a divergence stops the
    // run where it starts. The destinations come from the seed and the walk-to's index alone: a draw reading either
    // game would feed a divergence back into the input.
    constexpr u64 TICKS = 216'000;
    constexpr u64 WALK_EVERY = 3'600; // a walk-to each minute of play
    constexpr u64 LOG_EVERY = 21'600;
    constexpr u64 SEED = 0xC0FFEE;
    constexpr u32 PURPOSE = 1; // the only draw the script makes
    // Bursts end early only at a month's request, so twice the even split is far more than the run needs.
    constexpr u64 MAX_BURSTS = 2 * TICKS / core::REPLAY_HASH_TICKS;
    const Folder folderOne{ "hour_one_worker" };
    const Folder folderMany{ "hour_many_workers" };
    // The shipped data, on purpose: 50,000 NPCs are what gives 4 workers work to divide.
    Session one(folderOne.Path(), game::WORLD_SCENE, test::ShippedData(), 1);
    Session many(folderMany.Path(), game::WORLD_SCENE, test::ShippedData(), 4);
    REQUIRE(one.host.Current().key == game::WORLD_SCENE);
    REQUIRE(many.host.Current().key == game::WORLD_SCENE);
    REQUIRE(one.host.StateHash() == many.host.StateHash());
    REQUIRE(one.host.WorldHash() == many.host.WorldHash());

    const std::shared_ptr<const core::WorldMap> world = test::ShippedWorld();
    const core::WorldMap& map = *world;
    const std::span<const core::Place> places = map.Places();
    u32 walks = 0;
    u64 nextWalk = 0;
    u64 nextLog = LOG_EVERY;
    u64 bursts = 0;
    for (u64 tick = many.logic.Counters().wall; tick < TICKS; tick = many.logic.Counters().wall) {
        ++bursts;
        REQUIRE_MESSAGE(bursts <= MAX_BURSTS, "the wall clock never reached tick ", TICKS, ": it stands at ", tick);
        if (tick >= nextWalk) {
            const u32 drawn = core::ToBelow(core::Random(SEED, 0, 0, PURPOSE, walks), static_cast<u32>(places.size()));
            const core::Vec2 centre = map.TileCentre(map.TileIndex(places[drawn].tile));
            // A walk-to onto the cultivator's own tile only stops it, so it sets no route.
            const bool away = map.TileAt(centre) != map.TileAt(FootOf(many.host, many.host.Player()));
            one.WalkTo(centre);
            many.WalkTo(centre);
            ++walks;
            nextWalk += WALK_EVERY;
            if (away) {
                // One tick, so a place next door is not reached and its route ended before the check: a walk-to the
                // host ignored would otherwise leave the run passing while the cultivator stands still.
                one.Burst(1);
                many.Burst(1);
                REQUIRE_MESSAGE(many.host.PlayerRoute().has_value(), "walk-to ", walks, " to place ", drawn, " set no route");
                tick = many.logic.Counters().wall;
            }
        }
        // To the next hash tick; a month's request ends a burst early, and the next realigns.
        const u64 burst = core::REPLAY_HASH_TICKS - (tick % core::REPLAY_HASH_TICKS);
        one.Burst(burst);
        many.Burst(burst);
        const u64 wall = many.logic.Counters().wall;
        REQUIRE_MESSAGE(one.logic.Counters().wall == wall, "parted by wall tick ", wall);
        REQUIRE_MESSAGE(one.host.StateHash() == many.host.StateHash(), "parted by wall tick ", wall);
        REQUIRE_MESSAGE(one.host.WorldHash() == many.host.WorldHash(), "parted by wall tick ", wall);
        if (wall >= nextLog) {
            PN_LOG_INFO(
                "wall tick {}: {} months, StateHash {:016x} WorldHash {:016x} alike on 1 and 4 workers, {} walk-tos sent",
                wall,
                many.host.Months(),
                many.host.StateHash(),
                many.host.WorldHash(),
                walks
            );
            nextLog += LOG_EVERY;
        }
    }
    CHECK(many.host.Months() >= (TICKS / game::TICKS_PER_MONTH) - 1);
}

} // namespace
