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
#include "core/tick_clock.hpp"
#include "game/arena.hpp"
#include "support/manifest.hpp"

#include <doctest/doctest.h>

namespace {

constexpr u32 WORKERS = 2;

u64 NoClock() {
    return 0;
}

// A game as app::Game holds one, built as `scene`, without its clock: the tests choose the bursts. As App does, it
// reports each scene a snapshot needs resident, at once.
struct Session {
    explicit Session(
        const std::filesystem::path& folder,
        game::Scene scene = game::Scene::Arena,
        const core::GameData& data = game::BuiltInData()
    )
      : saves(folder / "slot.pns", jobs)
      , replays(folder / "replay.pnr", jobs)
      , arena(test::TestManifest(), scene, data) { // parentheses: MSVC C4868
        Burst(1);                                  // waits, and reports the scene
        Burst(1);                                  // takes the report
    }

    void Burst(u64 ticks) {
        static_cast<void>(logic.RunBurst({ .ticks = ticks }));
        core::Snapshot* published = handover.TakeState();
        volleys = published->volleys.size();
        if (published->scene != reported) {
            reported = published->scene;
            handover.SendInput({ .kind = core::ActionKind::SceneResident, .scene = published->scene });
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
    game::Arena arena;
    core::Logic logic{ handover, arena, jobs, saves, replays, replay, NoClock };
    std::optional<core::SceneId> reported = std::nullopt;
    std::size_t volleys = 0; // in flight in the last snapshot published
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
        Session recording(folder.Path(), game::Scene::Cells);
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
        recording.Send(core::ActionKind::CycleScene); // to the arena
        recording.Burst(BURST);                       // ends after the change
        recording.Burst(BURST);                       // waits for the arena, which the session reports
        recording.Burst(BURST);                       // takes the report
        REQUIRE(recording.arena.Player() != core::StableId{});
        recording.Hold(RIGHT);
        recording.Burst(WALK);
        selected = recording.arena.Player();
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
        REQUIRE(recording.arena.Months() == 1);
        REQUIRE(recording.arena.Selected() == selected);
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
    CHECK(playing.arena.Months() == 1);
    CHECK(playing.arena.Selected() == selected);
    CHECK(playing.logic.Epoch() == 1);
}

// The living foxes' IDs, in order: none dying.
std::vector<core::StableId> Foxes(const game::Arena& arena) {
    std::vector<core::StableId> foxes;
    const core::Registry& entities = arena.State()[game::ACTIVE].Entities();
    for (auto [entity, creature, stable] : entities.view<const game::Creature, const core::StableId>().each()) {
        if (creature.kind == game::CreatureKind::Fox && !entities.all_of<game::Dying>(entity)) {
            foxes.push_back(stable);
        }
    }
    std::ranges::sort(foxes);
    return foxes;
}

// Where `entity`'s foot is now.
core::Vec2 FootOf(const game::Arena& arena, core::StableId entity) {
    return arena.State()[game::ACTIVE].Entities().get<game::Body>(arena.State()[game::ACTIVE].Find(entity)).current;
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
        Session recording(folder.Path(), game::Scene::Arena, data);
        const std::vector<core::StableId> first = Foxes(recording.arena);
        REQUIRE(first.size() >= 3);
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
        // Each at a fox still living: the lances kill theirs at once.
        for (u32 lance = 0; lance < 2; ++lance) {
            recording.Cast(core::Slot::Spender, FootOf(recording.arena, Foxes(recording.arena).front()));
            recording.Burst(LANCE_COOLING);
        }
        for (u32 bolt = 0; bolt < 3; ++bolt) {
            recording.Cast(core::Slot::Generator, FootOf(recording.arena, Foxes(recording.arena).front()));
            recording.Burst(BOLT_COOLING);
        }
        const core::Vec2 dashedFrom = FootOf(recording.arena, recording.arena.Player());
        recording.Cast(core::Slot::Movement, dashedFrom + DASH_TO);
        recording.Burst(DASHING);
        const f32 dashed = FootOf(recording.arena, recording.arena.Player()).x - dashedFrom.x; // straight right
        CHECK(dashed == doctest::Approx(data.abilities.Of(core::AbilityKind::Dash).distance));
        recording.Cast(core::Slot::Special1, FootOf(recording.arena, Foxes(recording.arena).back()));
        recording.Burst(TO_MID_VOLLEY);
        REQUIRE(recording.volleys > 0); // the swords fly
        recording.Send(core::ActionKind::Save);
        recording.Burst(TO_RESPAWNS);
        recording.Burst(core::REPLAY_HASH_TICKS - (recording.logic.Counters().wall % core::REPLAY_HASH_TICKS));
        REQUIRE(recording.logic.Counters().wall % core::REPLAY_HASH_TICKS == 0);
        const core::Registry& entities = recording.arena.State()[game::ACTIVE].Entities();
        CHECK(entities.get<game::Caster>(recording.arena.State()[game::ACTIVE].Find(recording.arena.Player())).qi == QI_LEFT);
        const std::vector<core::StableId> now = Foxes(recording.arena);
        CHECK(std::ranges::any_of(now, [&first](core::StableId fox) { return !std::ranges::contains(first, fox); }));
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
    } // its job system finishes writing the replay

    Session playing(folder.Path(), game::Scene::Arena, data);
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

} // namespace
