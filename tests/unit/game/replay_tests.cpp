// The phase 4 gate for replays (design doc 0002 §16, 0002.2), extended in phase 5 (0002.3 §9): an arena with
// every NPC system (walking active NPCs, a world of NPCs living months) recorded through the real logic, with
// the player walking and selecting, a pause, a calendar step and a save in it, plays back in a fresh game with every hash
// matching, the last one on the recording's final tick.
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

// A game as app::Game holds one, without its clock: the tests choose the bursts. Its scene is resident, as the
// main thread reports once it holds it.
struct Session {
    explicit Session(const std::filesystem::path& folder)
      : saves(folder / "slot.pns", jobs)
      , replays(folder / "replay.pnr", jobs) {
        handover.SendInput({ .kind = core::ActionKind::SceneResident, .scene = arena.Scene() });
        Burst(1);
    }

    void Burst(u64 ticks) {
        static_cast<void>(logic.RunBurst({ .ticks = ticks }));
        handover.ReturnState(handover.TakeState());
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
    game::Arena arena{ test::TestManifest(), game::Scene::Arena };
    core::Logic logic{ handover, arena, jobs, saves, replays, replay, NoClock };
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
        Session recording(folder.Path());
        recording.Send(core::ActionKind::ToggleRecording);
        recording.Burst(1);
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

    Session playing(folder.Path());
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
