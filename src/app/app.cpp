#include "app/app.hpp"

#include "app/game.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/math.hpp"
#include "core/profile.hpp"
#include "core/tick_clock.hpp"
#include "platform/clock.hpp"

namespace app {

namespace {

// Frames during a calendar step's transition: few, leaving the cores to the step (design doc 0002 §7.3).
constexpr u64 TRANSITION_FRAME_NS = core::NS_PER_SECOND / 20;

} // namespace

App::App(platform::Window window, gfx::Renderer renderer, Options options)
  : m_window{ std::move(window) }
  , m_renderer{ std::move(renderer) }
  , m_options{ std::move(options) }
  , m_startNs{ platform::NowNs() } {
    // Started here, not in the initializer list: every member it uses is built by now.
    m_logic = std::jthread{ [this](const std::stop_token& stop) { RunLogic(stop); } };
}

App::~App() {
    m_logic.request_stop();
    m_logic.join();
}

void App::RunLogic(const std::stop_token& stop) {
    // An exception that escapes is logged and stops the game (§12).
    try {
        core::math::SetFloatingPointEnvironment();
        {
            Game game(m_handover, m_options.scene, m_options.saveSlot, m_startNs, platform::NowNs);
            game.Run(stop);
        } // the workers join here: a hang at shutdown shows as no "stopped" line
        PN_LOG_INFO("logic thread and job system stopped");
    }
    catch (const std::exception& e) {
        PN_LOG_CRITICAL("logic thread failed: {}", e.what());
        m_logicFailed = true;
    }
    catch (...) {
        PN_LOG_CRITICAL("logic thread failed: an exception of unknown type");
        m_logicFailed = true;
    }
}

i32 App::Run() {
    for (;;) {
        PN_PROFILE_ZONE();
        const platform::WindowState& state = m_window.Poll(core::TICK_NS);
        if (state.quitRequested) {
            PN_LOG_DEBUG("quit requested");
            break;
        }
        if (m_options.quitAfterNs != 0 && platform::NowNs() - m_startNs >= m_options.quitAfterNs) {
            PN_LOG_INFO("quitting after {} seconds, as --quit-after asked", m_options.quitAfterNs / core::NS_PER_SECOND);
            break;
        }
        if (m_logicFailed) {
            return 1;
        }
        if (m_options.scene == game::Scene::Cells) {
            m_camera.zoom = gfx::ZoomedBy(m_camera.zoom, state.wheelNotches);
        }
        SendInput(state);
        Present(state.visible);
    }
    PN_LOG_INFO("shutting down; {} snapshots were published after they were due", m_display.LateAdoptions());
    return 0;
}

void App::SendInput(const platform::WindowState& state) {
    PN_PROFILE_ZONE();
    if (platform::PausesGame(state) != m_paused) {
        m_paused = !m_paused;
        PN_LOG_INFO("{}: the window is {}", m_paused ? "paused" : "resumed", m_paused ? "unfocused or minimised" : "back");
        m_handover.SendInput({ .kind = m_paused ? core::ActionKind::Pause : core::ActionKind::Resume });
    }
    if (state.saveRequested) {
        PN_LOG_INFO("a save asked for");
        m_handover.SendInput({ .kind = core::ActionKind::Save });
    }
    if (state.loadRequested) {
        PN_LOG_INFO("a load asked for");
        m_handover.SendInput({ .kind = core::ActionKind::Load });
    }
    if (state.monthRequested) {
        PN_LOG_INFO("a month asked to pass");
        m_handover.SendInput({ .kind = core::ActionKind::AdvanceCalendar });
    }
}

void App::Present(bool visible) {
    PN_PROFILE_ZONE();
    // Events are drained every iteration, drawing or not, so the queue never grows (§5).
    m_handover.TakeEvents(m_incoming);
    m_pending.insert(m_pending.end(), m_incoming.begin(), m_incoming.end());
    static_cast<void>(m_display.Update(m_handover, platform::NowNs(), core::DEFAULT_DISPLAY_DELAY_NS));
    const core::Snapshot* drawn = m_display.Drawn();
    if (drawn == nullptr) {
        return;
    }
    core::PresentFrame(
        *drawn,
        m_pending,
        visible,
        [this] { m_renderer.Acquire(); },
        platform::NowNs,
        // shortcut: nothing plays events yet; audio and particles (phase 7) replace the empty player.
        [](const core::Event& /*event*/) {},
        [this, drawn](f64 alpha) {
            m_renderer.Draw(*drawn, alpha, m_camera);
            PN_PROFILE_FRAME();
        }
    );
    if (drawn->transition) {
        // shortcut: nothing draws the transition itself yet; its view model and screen come with the UI (phase 6).
        platform::WaitUntilNs(platform::NowNs() + TRANSITION_FRAME_NS);
    }
}

} // namespace app
