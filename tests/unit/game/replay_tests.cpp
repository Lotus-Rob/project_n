// The phase 4 gate for replays (design doc 0002 §16, 0002.2), extended in phase 5 (0002.3 §9): an arena with
// every NPC system (walking active NPCs, a world of NPCs living months) recorded through the real logic, from the
// cells and changed to with F2, with the player walking and selecting, a pause, a calendar step and a save in it, plays
// back in a fresh game in another scene (a load across scenes, waiting for each) with every hash matching, the last
// one on the recording's final tick.
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
    explicit Session(const std::filesystem::path& folder, game::Scene scene = game::Scene::Arena)
      : saves(folder / "slot.pns", jobs)
      , replays(folder / "replay.pnr", jobs)
      , arena{ test::TestManifest(), scene } {
        Burst(1); // waits, and reports the scene
        Burst(1); // takes the report
    }

    void Burst(u64 ticks) {
        static_cast<void>(logic.RunBurst({ .ticks = ticks }));
        core::Snapshot* published = handover.TakeState();
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

} // namespace
