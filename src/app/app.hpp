#pragma once

/// @file
/// @ingroup app
/// The main thread's side of the game (design doc 0002 §5, §6): the systems it owns, the logic
/// thread it starts, and the main loop that turns the window's input into actions and draws the
/// snapshots the logic thread publishes.

#include "app/bindings.hpp"
#include "app/cast.hpp"
#include "app/damage_numbers.hpp"
#include "app/hud.hpp"
#include "app/view.hpp"
#include "core/effects.hpp"
#include "core/error.hpp"
#include "core/frame.hpp"
#include "core/game_data.hpp"
#include "core/jobs.hpp"
#include "core/manifest.hpp"
#include "core/settings.hpp"
#include "core/strings.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "game/arena.hpp"
#include "gfx/camera.hpp"
#include "gfx/renderer.hpp"
#include "platform/window.hpp"

#include <atomic>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace app {

/// How long `--cast-swords` waits after casting the swords before it casts them again, or as soon after as they are
/// ready and the Qi covers them.
inline constexpr u64 CAST_SWORDS_NS = 4 * core::NS_PER_SECOND;
/// Where `--cast-swords` casts, from the followed player's foot, in world units.
inline constexpr core::Vec2 CAST_SWORDS_OFFSET{ .x = 300.0F, .y = 0.0F };

/// What the command line and the platform chose for a run.
struct Options {
    /// The atlas manifest, read from `assets/manifest.yaml`; never null.
    std::shared_ptr<const core::Manifest> manifest = nullptr;
    /// The strings table, read from `assets/data/strings.yaml`, which the HUD shows; never null.
    std::shared_ptr<const core::Strings> strings = nullptr;
    /// The scene the logic thread builds.
    game::Scene scene = game::Scene::Arena;
    /// The game data, read from `assets/data/`.
    core::GameData data = game::BuiltInData();
    /// The effects' looks and the HUD's layout, read from `assets/data/effects.yaml`; debug builds reload them with the data.
    core::Effects effects{};
    /// Quit by itself after this long (`--quit-after`); 0 never does.
    u64 quitAfterNs = 0;
    /// Send a debug burst of hits once a second (`--burst`), so a profile run sees the damage numbers' stress without a key
    /// press.
    bool burst = false;
    /// The zoom every scene starts at (`--zoom`), from gfx::MIN_ZOOM to gfx::MAX_ZOOM, so a profile run can draw at either
    /// end; the wheel still changes it.
    f32 zoom = 1.0F;
    /// Cast the flying swords CAST_SWORDS_NS after the last cast, or as soon after as they are ready (about every 6 s),
    /// at the followed player's foot plus CAST_SWORDS_OFFSET (`--cast-swords`), topping the Qi up with the generator first when
    /// it is short (TimedCast()), so a profile run sees volleys without key presses.
    bool castSwords = false;
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// The player's settings, read from `settings.yaml`: the main thread's systems take theirs from it.
    core::Settings settings{};
    /// The save slot; empty when no folder for saves was found.
    std::filesystem::path saveSlot{};
    /// The replay file; empty when no folder for saves was found.
    std::filesystem::path replaySlot{};
    /// The folder the game data was read from, which debug builds watch to reload it; empty for none.
    std::filesystem::path dataFolder{};
    // NOLINTEND(readability-redundant-member-init)
};

/// Whether the command line `args` holds `flag` (as in `--burst`) as one of its arguments.
[[nodiscard]] bool HasFlag(std::span<const char* const> args, std::string_view flag);

/// The zoom `--zoom <value>` in the command line `args` asks every scene to start at, 1 without it; an error ("--zoom:
/// ...") for a missing value, text that isn't a number, or a zoom outside gfx::MIN_ZOOM to gfx::MAX_ZOOM.
[[nodiscard]] std::expected<f32, core::Error> ZoomOf(std::span<const char* const> args);

/// Watches a folder of files for changes (the game data, design doc 0002.3 §7).
class DataWatch {
public:
    /// Watches `folder` (none when empty) from its state now.
    explicit DataWatch(std::filesystem::path folder = {});

    /// Whether a file in the folder was added, removed or written since it was built or last asked.
    [[nodiscard]] bool Changed();

private:
    // Each file's name and last write, sorted by name.
    using Listing = std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>>;

    // `folder`'s listing; `error` says why when it, or a file in it, couldn't be read.
    [[nodiscard]] static Listing List(const std::filesystem::path& folder, std::error_code& error);

    std::filesystem::path m_folder;
    Listing m_seen;
    bool m_unreadable = false; // whether the last look failed, which was logged
};

/// What an edit in the data folder reloads: each part only when what it reads changed.
struct DataReload {
    /// The effects changed: the renderer and the HUD take them; the simulation never knows.
    bool effects = false;
    /// The game data changed: the simulation reloads it (ReloadData, which ends a replay playing).
    bool gameData = false;
};

/// What the data folder, read again after an edit, reloads: the effects when `readEffects` hashes unlike `effects` (those
/// drawn), the game data when `readData` hashes unlike `data` (that running). A part that couldn't be read reloads nothing;
/// an edit that changed neither (the strings, say) reloads nothing.
[[nodiscard]] DataReload ReloadOf(
    const core::Effects& effects,
    const std::expected<core::Effects, core::Error>& readEffects,
    const core::GameData& data,
    const std::expected<core::GameData, core::Error>& readData
);

/// The main thread's systems (the job system, the window, the renderer, the handover and its display
/// side, the camera, the damage numbers, the HUD) and the logic thread, which builds and runs a Game on the job system.
class App {
public:
    /// Takes over `window` and `renderer` (made for that window) and starts the logic thread for
    /// `options`.
    App(platform::Window window, gfx::Renderer renderer, Options options);
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;
    /// Stops the logic thread and waits for it, before the handover it uses goes.
    ~App();

    /// Runs the main loop until the player quits or `--quit-after` runs out (0), or the logic
    /// thread fails or a scene can't be loaded (1).
    [[nodiscard]] i32 Run();

private:
    void RunLogic(const std::stop_token& stop);
    void HandleInput(const platform::WindowState& state);
    void NoteDevice(const platform::InputEvent& input);
    void Select(const platform::WindowState& state, core::Vec2 position);
    void Cast(const platform::WindowState& state, const ControlEvent& control, platform::Device device, bool combat);
    void Pan(u64 nowNs);
    [[nodiscard]] bool Present(const platform::WindowState& state);
    [[nodiscard]] bool LoadScene(core::SceneId wanted);
    [[nodiscard]] std::optional<std::size_t> Hovered(
        const platform::WindowState& state,
        const core::Snapshot& drawn,
        f64 alpha
    ) const;
    // Reads the data folder again after an edit and reloads what changed (ReloadOf()); a broken edit is logged with its
    // line, and what it broke stays as it was.
    void WatchData(u64 nowNs);
    // Draws, and lays the HUD out, as `effects` says from the next frame: the simulation is never told.
    void ApplyEffects(const core::Effects& effects);
    void RepeatBurst(u64 nowNs);
    void CastOnTimer(u64 nowNs);
    void MeasureLatency(const core::Snapshot& drawn);

    // First, so it goes last: the logic thread registers with it, the renderer's loads run on it.
    core::Jobs m_jobs{ core::Jobs::DefaultWorkers(), /*externalThreads=*/1 };
    platform::Window m_window;
    gfx::Renderer m_renderer;
    Options m_options;
    u64 m_startNs;
    u64 m_nextBurstNs;       // when `--burst`'s next burst is sent, in wall time (platform::NowNs())
    u64 m_nextCastNs;        // when `--cast-swords` next casts, at the earliest, likewise
    DamageNumbers m_numbers; // as m_options.settings say
    Hud m_hud;               // likewise
    CastLatency m_swordsLatency{ core::AbilityKind::FlyingSwords };
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    core::GameHandover m_handover{};
    core::GameDisplay m_display{};
    std::vector<core::Event> m_incoming{};
    std::vector<core::Event> m_pending{};        // taken, waiting for the render clock
    std::vector<core::TextItem> m_numberTexts{}; // the damage numbers drawn this frame
    std::vector<gfx::HudLine> m_hudLines{};      // the HUD drawn this frame
    std::vector<gfx::HudBar> m_hudBars{};
    std::vector<platform::InputEvent> m_inputs{};
    Bindings m_bindings{};
    // NOLINTEND(readability-redundant-member-init)
    gfx::Camera m_camera{};
    FrameClock m_frameClock{};                              // what labels fade by
    core::HeldInput m_held{};                               // as last sent
    u64 m_frameNs = 0;                                      // when the last frame began, for panning
    u64 m_dataCheckNs = 0;                                  // when the data folder is looked at next
    u64 m_loadStartNs = 0;                                  // when the scene loading began
    std::optional<core::SceneId> m_loading = std::nullopt;  // the scene the renderer loads
    std::optional<core::SceneId> m_reported = std::nullopt; // the scene the logic was told is resident, last
    std::optional<core::SceneId> m_shown = std::nullopt;    // the scene of the snapshot drawn last
    std::optional<u64> m_numbersEpoch = std::nullopt;       // the timeline the damage numbers belong to
    DataWatch m_dataWatch{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    DeviceInUse m_device{};
    bool m_quit = false;
    bool m_paused = false;
    std::atomic<bool> m_logicFailed = false;
    std::jthread m_logic{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer; last, so it
                            // stops and joins before anything it uses goes
};

} // namespace app
