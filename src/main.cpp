#include "app/app.hpp"
#include "core/command_line.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "data/game_data.hpp"
#include "data/manifest.hpp"
#include "data/settings.hpp"
#include "game/arena.hpp"
#include "gfx/renderer.hpp"
#include "platform/paths.hpp"
#include "platform/sdl_log.hpp"
#include "platform/window.hpp"

namespace {

// `--<name>` for any scene of the atlas (art/scenes.toml) the game knows: `--stress`, `--painted`, `--cells`; the
// arena by default.
game::Scene ChooseScene(std::span<char*> args) {
    for (const std::string_view arg : args) {
        if (const std::optional<game::Scene> scene = arg.starts_with("--") ? game::SceneNamed(arg.substr(2)) : std::nullopt) {
            return *scene;
        }
    }
    return game::Scene::Arena;
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

// The game data's folder: `assets/data/`, or in debug builds the source tree's while it exists.
std::filesystem::path DataFolder(const std::filesystem::path& assets) {
#ifdef PN_SOURCE_DATA
    std::error_code error;
    if (const std::filesystem::path source{ PN_SOURCE_DATA }; std::filesystem::is_directory(source, error)) {
        return source;
    }
#endif
    return assets / "data";
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
    std::expected<core::Manifest, core::Error> manifest = data::LoadManifest(*assets / "manifest.yaml");
    if (!manifest) {
        return FailToStart("the game's files are damaged", manifest.error());
    }
    options.manifest = std::make_shared<const core::Manifest>(std::move(*manifest));
    PN_LOG_INFO("manifest: {} sprites and {} scenes", options.manifest->sprites.size(), options.manifest->scenes.size());
    const std::filesystem::path dataFolder = DataFolder(*assets);
    const std::expected<core::GameData, core::Error> gameData = data::LoadGameData(dataFolder);
    if (!gameData) {
        return FailToStart("the game's data couldn't be read", gameData.error());
    }
    if (const std::expected<void, core::Error> runs = game::CheckScenes(*options.manifest, *gameData); !runs) {
        return FailToStart("the game's files are damaged", runs.error());
    }
    PN_LOG_INFO("game data read from {}", dataFolder.string());
    options.data = *gameData;
    options.dataFolder = dataFolder;
    std::expected<gfx::Renderer, core::Error> renderer = gfx::Renderer::Create(*window, *assets, options.manifest);
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
    const game::Scene scene = ChooseScene(args);
    PN_LOG_INFO("scene: {}", game::SceneName(scene));
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
                    .scene = scene,
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
