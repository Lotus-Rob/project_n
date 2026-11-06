#pragma once

/// @file
/// @ingroup app
/// The main thread's side of the game (design doc 0002 §5, §6): the systems it owns, the logic
/// thread it starts, and the main loop that turns the window's input into actions and draws the
/// snapshots the logic thread publishes.

#include "core/frame.hpp"
#include "core/types.hpp"
#include "game/arena.hpp"
#include "gfx/camera.hpp"
#include "gfx/renderer.hpp"
#include "platform/window.hpp"

#include <atomic>
#include <filesystem>
#include <stop_token>
#include <thread>
#include <vector>

namespace app {

/// What the command line and the platform chose for a run.
struct Options {
    /// The scene the logic thread builds.
    game::Scene scene = game::Scene::Arena;
    /// Quit by itself after this long (`--quit-after`); 0 never does.
    u64 quitAfterNs = 0;
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    /// The save slot; empty when no folder for saves was found.
    std::filesystem::path saveSlot{};
    /// The replay file; empty when no folder for saves was found.
    std::filesystem::path replaySlot{};
    // NOLINTEND(readability-redundant-member-init)
};

/// The main thread's systems (the window, the renderer, the handover and its display side, the
/// camera) and the logic thread, which builds and runs a Game.
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
    /// thread fails (1).
    [[nodiscard]] i32 Run();

private:
    void RunLogic(const std::stop_token& stop);
    void SendInput(const platform::WindowState& state);
    void Present(bool visible);

    platform::Window m_window;
    gfx::Renderer m_renderer;
    Options m_options;
    u64 m_startNs;
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    core::GameHandover m_handover{};
    core::GameDisplay m_display{};
    std::vector<core::Event> m_incoming{};
    std::vector<core::Event> m_pending{}; // taken, waiting for the render clock
    // NOLINTEND(readability-redundant-member-init)
    gfx::Camera
        m_camera{}; // shortcut: a fixed view, but for the cell grid's zoom; it follows input and the snapshot from phase 5
    bool m_paused = false;
    std::atomic<bool> m_logicFailed = false;
    std::jthread m_logic{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer; last, so it
                            // stops and joins before anything it uses goes
};

} // namespace app
