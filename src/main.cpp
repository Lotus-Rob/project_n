#include "app/app.hpp"
#include "core/command_line.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "data/game_data.hpp"
#include "data/settings.hpp"
#include "game/arena.hpp"
#include "gfx/renderer.hpp"
#include "platform/paths.hpp"
#include "platform/sdl_log.hpp"
#include "platform/window.hpp"

namespace {

struct SceneChoice {
    game::Scene scene = game::Scene::Arena;
    std::string_view name = "the arena";
};

SceneChoice ChooseScene(std::span<char*> args) {
    if (std::ranges::contains(args, std::string_view{ "--painted" })) {
        return { .scene = game::Scene::Painted, .name = "the painted test scene" };
    }
    if (std::ranges::contains(args, std::string_view{ "--cells" })) {
        return { .scene = game::Scene::Cells, .name = "the map cell grid" };
    }
    if (std::ranges::contains(args, std::string_view{ "--stress" })) {
        return { .scene = game::Scene::Stress, .name = "the stress arena" };
    }
    return {};
}

// `--quit-after SECONDS`: quit by itself, as on a quit request, so `project_n profile` records with no click into a
// window that would take the focus. 0: never.
std::expected<u64, core::Error> QuitAfterNs(std::span<char*> args) {
    const auto flag = std::ranges::find(args, std::string_view{ "--quit-after" });
    if (flag == args.end()) {
        return 0;
    }
    return core::SecondsToNs(std::next(flag) == args.end() ? std::string_view{} : std::string_view{ *std::next(flag) })
        .transform_error([](const core::Error& error) { return core::Error{ "--quit-after: " + error.message }; });
}

// A start-up failure: reported (logged and shown), and the game's exit status.
i32 FailToStart(std::string_view what, const core::Error& error) {
    platform::ReportStartFailure(what, error);
    return 1;
}

// Opens the window (as `settings` say) and the renderer, then hands them to the app; a failure to start is reported, not
// thrown.
i32 Run(const core::Settings& settings, app::Options options) {
    platform::RouteSdlLogs();
    std::expected<platform::Window, core::Error> window =
        platform::Window::Open("project_n", settings.windowWidth, settings.windowHeight, settings.fullscreen);
    if (!window) {
        return FailToStart("the window couldn't be opened", window.error());
    }
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    if (!assets) {
        return FailToStart("the game's files weren't found", assets.error());
    }
    const std::expected<core::GameData, core::Error> gameData = data::LoadGameData(*assets / "data");
    if (!gameData) {
        return FailToStart("the game's data couldn't be read", gameData.error());
    }
    PN_LOG_INFO("game data read from {}", (*assets / "data").string());
    options.data = *gameData;
    std::expected<gfx::Renderer, core::Error> renderer = gfx::Renderer::Create(*window, *assets);
    if (!renderer) {
        return FailToStart("the graphics card couldn't be used", renderer.error());
    }
    app::App app(std::move(*window), std::move(*renderer), std::move(options));
    return app.Run();
}

} // namespace

int main(int argc, char** argv) {
    const std::expected<std::filesystem::path, core::Error> userData = platform::UserDataDirectory();
    core::log::Start(userData ? *userData / "logs" : std::filesystem::path{});
    if (!userData) {
        PN_LOG_WARNING("logs go to the console only: {}", userData.error().message);
    }
    PN_LOG_NOTICE("project_n starting");
#ifdef __clang__
#pragma clang diagnostic push
// The C runtime hands the arguments over as a pointer and a count: nothing can check them.
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
    const std::span<char*> args{ argv, static_cast<std::size_t>(argc) };
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    const SceneChoice choice = ChooseScene(args);
    PN_LOG_INFO("scene: {}", choice.name);
    const std::expected<u64, core::Error> quitAfterNs = QuitAfterNs(args);
    if (!quitAfterNs) {
        PN_LOG_ERROR("{}", quitAfterNs.error().message);
        return 1;
    }
    try {
        // shortcut: one save slot; slots and their screen come with the UI (phase 6).
        const i32 status =
            Run(userData ? data::LoadSettings(*userData / "settings.yaml") : core::Settings{},
                {
                    .scene = choice.scene,
                    .quitAfterNs = *quitAfterNs,
                    .saveSlot = userData ? *userData / "saves" / "quicksave.pns" : std::filesystem::path{},
                    .replaySlot = userData ? *userData / "saves" / "replay.pnr" : std::filesystem::path{},
                });
        PN_LOG_NOTICE("project_n stopped with status {}", status);
        return status;
    }
    catch (const std::exception& e) {
        PN_LOG_CRITICAL("fatal: {}", e.what());
        return 1;
    }
    catch (...) {
        PN_LOG_CRITICAL("fatal: an exception of unknown type");
        return 1;
    }
}
