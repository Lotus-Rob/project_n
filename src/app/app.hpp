#pragma once

/// @file
/// @ingroup app
/// The main thread's side of the game: the systems it owns, the logic
/// thread it starts, and the main loop that turns the window's input into actions and draws the
/// snapshots the logic thread publishes.

#include "app/bindings.hpp"
#include "app/camera_control.hpp"
#include "app/camera_path.hpp"
#include "app/cast.hpp"
#include "app/damage_numbers.hpp"
#include "app/fog_fade.hpp"
#include "app/hud.hpp"
#include "app/target_cursor.hpp"
#include "app/view.hpp"
#include "core/display_delay.hpp"
#include "core/effects.hpp"
#include "core/error.hpp"
#include "core/frame.hpp"
#include "core/game_data.hpp"
#include "core/jobs.hpp"
#include "core/manifest.hpp"
#include "core/scene_reports.hpp"
#include "core/seals.hpp"
#include "core/settings.hpp"
#include "core/strings.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "core/world_look.hpp"
#include "game/host.hpp"
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

/// The most worker threads `--workers` accepts.
inline constexpr u32 MAX_WORKERS = 64;

/// What the command line and the platform chose for a run.
struct Options {
    /// The atlas manifest, read from `assets/manifest.yaml`; never null.
    std::shared_ptr<const core::Manifest> manifest = nullptr;
    /// The strings table, read from `assets/data/strings.yaml`, which the HUD shows; never null.
    std::shared_ptr<const core::Strings> strings = nullptr;
    /// The scene the logic thread builds.
    core::SceneKey scene = game::ARENA_SCENE;
    /// The game data, read from `assets/data/`.
    core::GameData data = game::BuiltInData();
    /// The effects' looks and the HUD's layout, read from `assets/data/effects.yaml`; debug builds reload them with the data.
    core::Effects effects{};
    /// The seals' looks, read from `assets/data/seals.yaml` against `manifest`; debug builds reload them with the data.
    core::Seals seals{};
    /// The world map's look, read from `assets/data/world_look.yaml` against `manifest` and the map; debug builds reload it
    /// with the data.
    core::WorldLook worldLook{};
    /// Quit by itself after this long (`--quit-after`); 0 never does.
    u64 quitAfterNs = 0;
    /// Send a debug burst of hits once a second (`--burst`), so a profile run sees the damage numbers' stress without a key
    /// press.
    bool burst = false;
    /// Start with the whole world map seen through the fog (`--debug-sight`); F3 flips it.
    bool debugSight = false;
    /// The zoom every scene starts at (`--zoom`), from gfx::MIN_ZOOM to gfx::MAX_ZOOM, so a profile run can draw at either
    /// end; the wheel still changes it.
    f32 zoom = 1.0F;
    /// Cast the flying swords CAST_SWORDS_NS after the last cast, or as soon after as they are ready (about every 6 s),
    /// at the followed player's foot plus CAST_SWORDS_OFFSET (`--cast-swords`), topping the Qi up with the generator first when
    /// it is short (TimedCast()), so a profile run sees volleys without key presses.
    bool castSwords = false;
    /// How many worker threads the job system runs (`--workers`); 0 takes core::Jobs::DefaultWorkers().
    u32 workers = 0;
    /// Present as fast as the display allows, immediate before mailbox (`--unpaced`), so a measurement isn't capped by the
    /// display's rate.
    bool unpaced = false;
    /// Drive the camera along the scripted path (CameraPath()) from CAMERA_PATH_DELAY_NS after the scene runs, with its
    /// scripted Move, and quit after it (`--camera-path`), logging each segment's start and the Move's input latency, for the
    /// measurements.
    bool cameraPath = false;
    /// Let one month pass at this second of the camera path (`--month-step-at`, MonthStepAtOf()), logging `camera path:
    /// month step`, so the step's cost shows in a measurement; none for no step.
    std::optional<f32> monthStepAt = std::nullopt;
    /// The player's settings, read from `settings.yaml`: the main thread's systems take theirs from it.
    // NOLINTNEXTLINE(readability-redundant-member-init): designated initializers omit it; Clang warns without one
    core::Settings settings{};
    /// The save slot; empty when no folder for saves was found.
    std::filesystem::path saveSlot;
    /// The replay file; empty when no folder for saves was found.
    std::filesystem::path replaySlot;
    /// The folder the game data was read from, which debug builds watch to reload it; empty for none.
    // NOLINTNEXTLINE(readability-redundant-member-init): designated initializers omit it; Clang warns without one
    std::filesystem::path dataFolder{};
};

/// Whether the command line `args` holds `flag` (as in `--burst`) as one of its arguments.
[[nodiscard]] bool HasFlag(std::span<const char* const> args, std::string_view flag);

/// The zoom `--zoom <value>` in the command line `args` asks every scene to start at, 1 without it; an error ("--zoom:
/// ...") for a missing value, text that isn't a number, or a zoom outside gfx::MIN_ZOOM to gfx::MAX_ZOOM.
[[nodiscard]] std::expected<f32, core::Error> ZoomOf(std::span<const char* const> args);

/// The job system's worker count `--workers <count>` in the command line `args` asks for, 0 without it (the default for the
/// machine); an error ("--workers: ...") for a missing value, text that isn't a number, or a count outside 1 to
/// MAX_WORKERS.
[[nodiscard]] std::expected<u32, core::Error> WorkersOf(std::span<const char* const> args);

/// The travel share `--travel-share <per mille>` in the command line `args` asks the game data's world map to have
/// (game::SetTravelShare()), none without it; an error ("--travel-share: ...") for a missing value, text that isn't a whole
/// number, or a share past core::MOST_SHARE_PER_MILLE.
[[nodiscard]] std::expected<std::optional<u16>, core::Error> TravelShareOf(std::span<const char* const> args);

/// The second of the camera path `--month-step-at <seconds>` in the command line `args` asks a month to pass at, none
/// without it; an error ("--month-step-at: ...") for a missing value, text that isn't a number, a negative or infinite
/// time, a time past the path's end (CameraPathOver()), or the flag without `--camera-path`, which it needs.
[[nodiscard]] std::expected<std::optional<f32>, core::Error> MonthStepAtOf(std::span<const char* const> args);

/// Watches a folder of files for changes (the game data).
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

/// Makes the game data `read` after an edit carry the world map `running` was started with: the map is read at the
/// start only, so an edit to `world.yaml` alone reloads nothing. Does nothing to data that couldn't be read.
void KeepWorld(const core::GameData& running, std::expected<core::GameData, core::Error>& read);

/// Makes `current` the seals table `read` after an edit, and says whether it did: a table that couldn't be read leaves
/// `current` as it is, as the reader hands back no part of a broken one.
[[nodiscard]] bool KeepSeals(core::Seals& current, std::expected<core::Seals, core::Error> read);

/// Makes `current` the world look `read` after an edit when it differs (by Hash()), and says whether it did: a look that
/// couldn't be read, or reads as it was, leaves `current` as it is.
[[nodiscard]] bool KeepLook(core::WorldLook& current, std::expected<core::WorldLook, core::Error> read);

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
    // Sends the held input when it changed: the player's Move, or `--camera-path`'s while it runs, timing its press.
    void SendHeld();
    // Hands the camera's controls (drag, zoom, snap back) to it: true when `control` was one of them, pressed.
    [[nodiscard]] bool SteerCamera(const ControlEvent& control);
    void Click(const platform::WindowState& state, core::Vec2 position);
    // Acts on the debug keys that change only what the main thread draws: true when `control` was one.
    [[nodiscard]] bool TakeDisplayToggle(Control control);
    // Acts on the cursor's two controls: true when `control` was one.
    [[nodiscard]] bool TakeCursorControl(Control control);
    // Shows the target cursor on the followed cultivator's tile, or hides it; nothing shows off the world map.
    void ToggleCursor();
    // Hides the cursor, and the renderer's outline of it.
    void HideCursor();
    // Walks to the cursor's tile and hides it.
    void ConfirmCursor();
    // Steers the cursor by this frame's held movement and draws it; hides it when the scene stops showing the map.
    void SteerCursor();
    void Cast(const platform::WindowState& state, const ControlEvent& control, platform::Device device, bool combat);
    void MoveCamera(const platform::WindowState& state, const core::Snapshot& drawn, f64 alpha);
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
    // Moves `--camera-path` on to `nowNs`: starts it, logs each segment's start, and quits after it.
    void FollowPath(u64 nowNs);
    void MeasureLatency(const core::Snapshot& drawn, f64 alpha);

    // First, so it goes last: the logic thread registers with it, the renderer's loads run on it.
    core::Jobs m_jobs;
    platform::Window m_window;
    gfx::Renderer m_renderer;
    Options m_options;
    u64 m_startNs;
    u64 m_nextBurstNs;       // when `--burst`'s next burst is sent, in wall time (platform::NowNs())
    u64 m_nextCastNs;        // when `--cast-swords` next casts, at the earliest, likewise
    DamageNumbers m_numbers; // as m_options.settings say
    Hud m_hud;               // likewise
    CastLatency m_swordsLatency{ core::AbilityKind::FlyingSwords };
    MoveLatency m_moveLatency{}; // `--camera-path`'s scripted press
    core::GameHandover m_handover;
    core::GameDisplay m_display;
    core::DisplayDelay m_delay;                // presentation only: never sent to the logic thread
    std::vector<core::Event> m_pending;        // taken, waiting for the render clock
    std::vector<core::TextItem> m_numberTexts; // the damage numbers drawn this frame
    std::vector<gfx::HudLine> m_hudLines;      // the HUD drawn this frame
    std::vector<gfx::HudBar> m_hudBars;
    std::vector<platform::InputEvent> m_inputs;
    Bindings m_bindings;
    CameraControl m_camera;                                  // starts each scene at m_options.zoom
    Drag m_drag{};                                           // the camera's, by the pointer
    FrameClock m_frameClock{};                               // what labels fade by
    f64 m_frameSeconds = 0.0;                                // the frame clock at the last drawn frame
    FogFade m_fog{};                                         // the world map's fog as drawn
    TargetCursor m_cursor{};                                 // the keyboard and gamepad's way to set a route
    u64 m_cursorNs = 0;                                      // when the cursor last steered
    core::HeldInput m_held{};                                // as last sent
    u64 m_cameraNs = 0;                                      // when the camera last moved on
    std::optional<u64> m_cameraChange = std::nullopt;        // the scene change the camera last showed
    u64 m_dataCheckNs = 0;                                   // when the data folder is looked at next
    u64 m_loadStartNs = 0;                                   // when the scene loading began
    std::optional<core::SceneId> m_loading = std::nullopt;   // the scene the renderer loads
    std::optional<core::SceneId> m_reported = std::nullopt;  // the scene the logic was told is resident, last
    core::SceneReports m_sceneReports{};                     // the ways out and in reported played
    std::optional<u64> m_numbersEpoch = std::nullopt;        // the timeline the damage numbers belong to
    std::optional<u64> m_pathStartNs = std::nullopt;         // when `--camera-path` starts, once the scene runs
    std::optional<f32> m_pathSeconds = std::nullopt;         // how far into it this frame is, while it runs
    std::optional<std::size_t> m_pathSegment = std::nullopt; // the segment last logged
    PathMonthStep m_monthStep;                               // `--month-step-at`'s one step
    DataWatch m_dataWatch;
    DeviceInUse m_device{};
    bool m_quit = false;
    bool m_showcase = true;    // whether the arena's showcase seals are drawn (F8)
    bool m_debugSight = false; // whether the whole map shows through the fog (F3, `--debug-sight`)
    bool m_paused = false;
    std::atomic<bool> m_logicFailed = false;
    std::jthread m_logic; // last, so it stops and joins before anything it uses goes
};

} // namespace app
