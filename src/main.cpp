#include "app/app.hpp"
#include "build_info.hpp"
#include "core/command_line.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/path_text.hpp"
#include "data/animations.hpp"
#include "data/effects.hpp"
#include "data/game_data.hpp"
#include "data/manifest.hpp"
#include "data/names.hpp"
#include "data/seals.hpp"
#include "data/settings.hpp"
#include "data/strings.hpp"
#include "data/world_look.hpp"
#include "data/world_map.hpp"
#include "game/scenes.hpp"
#include "game/travel.hpp"
#include "gfx/renderer.hpp"
#include "platform/paths.hpp"
#include "platform/sdl_log.hpp"
#include "platform/system.hpp"
#include "platform/window.hpp"

namespace {

/// `--<name>` for any scene of the atlas (art/scenes.toml) the game knows: `--stress`, `--painted`, `--cells`; the
/// arena by default.
core::SceneKey ChooseScene(std::span<char*> args) {
    for (const std::string_view arg : args) {
        if (const std::optional<core::SceneKey> scene =
                arg.starts_with("--") ? game::SceneNamed(arg.substr(2)) : std::nullopt) {
            return *scene;
        }
    }
    return game::ARENA_SCENE;
}

/// `--quit-after SECONDS`: quit by itself, as on a quit request, so `project_n profile` records with no click into a
/// window that would take the focus. 0: never.
std::expected<u64, core::Error> QuitAfterNs(std::span<char*> args) {
    const auto flag = std::ranges::find(args, std::string_view{ "--quit-after" });
    if (flag == args.end()) {
        return 0;
    }
    return core::SecondsToNs(std::next(flag) == args.end() ? std::string_view{} : std::string_view{ *std::next(flag) })
        .transform_error([](const core::Error& error) { return core::Error{ "--quit-after: " + error.message }; });
}

/// The game data's folder: `assets/data/`, or in debug builds the source tree's while it exists.
std::filesystem::path DataFolder(const std::filesystem::path& assets) {
#ifdef PN_SOURCE_DATA
    std::error_code error;
    if (const std::filesystem::path source = core::PathOf(PN_SOURCE_DATA); std::filesystem::is_directory(source, error)) {
        return source;
    }
#endif
    return assets / "data";
}

/// A start-up failure: reported (logged and shown), and the game's exit status.
i32 FailToStart(std::string_view what, const core::Error& error) {
    platform::ReportStartFailure(what, error, core::log::Folder());
    return 1;
}

/// A failure after the game started: reported (logged and shown), and the game's exit status.
i32 StopOnFailure(std::string_view what, const core::Error& error) {
    platform::ReportFailure("project_n has stopped", what, error, core::log::Folder());
    return 1;
}

/// Opens the window (as `settings` say) and the renderer, then hands them to the app, its world map's travel share set to
/// `travelShare` when there is one; a failure to start is reported, not thrown.
i32 Run(const core::Settings& settings, app::Options options, bool fullscreen, std::optional<u16> travelShare) {
    platform::RouteSdlLogs();
    std::expected<platform::Window, core::Error> window =
        platform::Window::Open("project_n", settings.windowWidth, settings.windowHeight, settings.fullscreen || fullscreen);
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
    PN_LOG_INFO("game data read from {}", core::PathText(dataFolder));
    options.data = *gameData;
    options.dataFolder = dataFolder;
    const std::expected<core::Effects, core::Error> effects = data::LoadEffects(dataFolder);
    if (!effects) {
        return FailToStart("the game's data couldn't be read", effects.error());
    }
    options.effects = *effects;
    std::expected<core::Seals, core::Error> seals = data::LoadSeals(dataFolder, *options.manifest);
    if (!seals) {
        return FailToStart("the game's data couldn't be read", seals.error());
    }
    options.seals = std::move(*seals);
    std::expected<core::Animations, core::Error> animations = data::LoadAnimations(dataFolder, *options.manifest);
    if (!animations) {
        return FailToStart("the game's data couldn't be read", animations.error());
    }
    options.animations = std::make_shared<const core::Animations>(std::move(*animations));
    std::expected<core::Strings, core::Error> strings = data::LoadStrings(dataFolder);
    if (!strings) {
        return FailToStart("the game's data couldn't be read", strings.error());
    }
    // The map names its places by the strings, so it is read after them; it is read once and the data watch keeps it.
    const std::expected<std::shared_ptr<const core::WorldMap>, core::Error> world = data::LoadWorldMap(dataFolder, *strings);
    if (!world) {
        return FailToStart("the game's data couldn't be read", world.error());
    }
    options.data.world = *world;
    // Before the game starts, so the share goes through the data's hash as the map's own would.
    if (travelShare) {
        if (const std::expected<void, core::Error> set = game::SetTravelShare(options.data, *travelShare); !set) {
            return FailToStart("the travel share couldn't be set", set.error());
        }
        PN_LOG_INFO("the world's travel share is {} per thousand", *travelShare);
    }
    // The names are read once like the map: the data watch keeps them.
    std::expected<core::Names, core::Error> names = data::LoadNames(dataFolder / "names.yaml");
    if (!names) {
        return FailToStart("the game's data couldn't be read", names.error());
    }
    options.data.names = std::make_shared<const core::Names>(std::move(*names));
    // The world scene needs the map, so the scenes are checked once it is in the data.
    if (const std::expected<void, core::Error> runs = game::CheckScenes(*options.manifest, options.data); !runs) {
        return FailToStart("the game's files are damaged", runs.error());
    }
    // The look names its sprites by the manifest and its kinds and danger sorts by the map, so it is read with them.
    std::expected<core::WorldLook, core::Error> worldLook = data::LoadWorldLook(dataFolder, *options.manifest, **world);
    if (!worldLook) {
        return FailToStart("the game's data couldn't be read", worldLook.error());
    }
    options.worldLook = std::move(*worldLook);
    PN_LOG_INFO("world map: {} by {} tiles, {} places", (*world)->Columns(), (*world)->Rows(), (*world)->Places().size());
    options.strings = std::make_shared<const core::Strings>(std::move(*strings));
    std::expected<gfx::Renderer, core::Error> renderer = gfx::Renderer::Create(
        *window,
        *assets,
        options.manifest,
        options.strings,
        { .language = settings.language, .outline = settings.textOutline, .shadow = settings.textShadow },
        settings.uiColours,
        options.unpaced
    );
    if (!renderer) {
        return FailToStart("the graphics card couldn't be used", renderer.error());
    }
    options.settings = settings;
    app::App app(std::move(*window), std::move(*renderer), std::move(options));
    if (const std::expected<void, app::Failure> ran = app.Run(); !ran) {
        return StopOnFailure(ran.error().what, ran.error().error);
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::expected<std::filesystem::path, core::Error> userData = platform::UserDataDirectory();
    core::log::Start(userData ? *userData / "logs" : std::filesystem::path{});
    if (!userData) {
        PN_LOG_WARNING("logs go to the console only: {}", userData.error().message);
    }
    PN_LOG_NOTICE("project_n {} ({}), {}", PN_GAME_VERSION, PN_GAME_COMMIT, PN_BUILD);
    PN_LOG_NOTICE("running on {}", platform::SystemSummary());
#ifdef __clang__
#pragma clang diagnostic push
// The C runtime hands the arguments over as a pointer and a count: nothing can check them.
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
    const std::span<char*> args{ argv, static_cast<std::size_t>(argc) };
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    const core::SceneKey scene = ChooseScene(args);
    PN_LOG_INFO("scene: {}", game::SceneName(scene));
    const std::expected<u64, core::Error> quitAfterNs = QuitAfterNs(args);
    if (!quitAfterNs) {
        PN_LOG_ERROR("{}", quitAfterNs.error().message);
        return 1;
    }
    const std::expected<f32, core::Error> zoom = app::ZoomOf(args);
    if (!zoom) {
        PN_LOG_ERROR("{}", zoom.error().message);
        return 1;
    }
    const std::expected<u32, core::Error> workers = app::WorkersOf(args);
    if (!workers) {
        PN_LOG_ERROR("{}", workers.error().message);
        return 1;
    }
    const std::expected<std::optional<u16>, core::Error> travelShare = app::TravelShareOf(args);
    if (!travelShare) {
        PN_LOG_ERROR("{}", travelShare.error().message);
        return 1;
    }
    const std::expected<std::optional<f32>, core::Error> monthStepAt = app::MonthStepAtOf(args);
    if (!monthStepAt) {
        PN_LOG_ERROR("{}", monthStepAt.error().message);
        return 1;
    }
    if (*workers != 0) {
        PN_LOG_INFO("the job system is pinned to {} workers", *workers);
    }
    if (*zoom != 1.0F) {
        PN_LOG_INFO("every scene starts at zoom {}", *zoom);
    }
    try {
        // One save slot: there is no slot choice or screen for it yet.
        const i32 status =
            Run(userData ? data::LoadSettings(*userData / "settings.yaml") : core::Settings{},
                {
                    .scene = scene,
#ifdef PN_ASSERTS
                    .failFrames = app::HasFlag(args, "--fail-frames"),
#endif
                    .quitAfterNs = *quitAfterNs,
                    .burst = app::HasFlag(args, "--burst"),
                    .debugSight = app::HasFlag(args, "--debug-sight"),
                    .zoom = *zoom,
                    .castSwords = app::HasFlag(args, "--cast-swords"),
                    .workers = *workers,
                    .unpaced = app::HasFlag(args, "--unpaced"),
                    .cameraPath = app::HasFlag(args, "--camera-path"),
                    .monthStepAt = *monthStepAt,
                    .saveSlot = userData ? *userData / "saves" / "quicksave.pns" : std::filesystem::path{},
                    .replaySlot = userData ? *userData / "saves" / "replay.pnr" : std::filesystem::path{},
                },
                app::HasFlag(args, "--fullscreen"),
                *travelShare);
        PN_LOG_NOTICE("project_n stopped with status {}", status);
        return status;
    }
    catch (const std::exception& e) {
        return StopOnFailure("an unexpected error", core::Error{ e.what() });
    }
    catch (...) {
        return StopOnFailure("an unexpected error", core::Error{ "an exception of unknown type" });
    }
}
