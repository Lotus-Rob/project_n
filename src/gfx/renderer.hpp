#pragma once

/// @file
/// @ingroup gfx
/// Drawing frames into the window with SDL_GPU.

#include "core/effects.hpp"
#include "core/error.hpp"
#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/manifest.hpp"
#include "core/names.hpp"
#include "core/seals.hpp"
#include "core/strings.hpp"
#include "core/types.hpp"
#include "core/ui_colours.hpp"
#include "core/vec2.hpp"
#include "core/world_look.hpp"
#include "core/world_map.hpp"
#include "gfx/camera.hpp"
#include "gfx/hud_draw.hpp"
#include "gfx/image.hpp"
#include "platform/window.hpp"

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace gfx {

/// How the swapchain presents a frame.
enum class PresentMode : u8 {
    /// At once, tearing allowed: nothing waits for the display.
    Immediate,
    /// The newest finished frame replaces the one waiting for the display's next refresh.
    Mailbox,
    /// On the display's refresh, each frame in turn; every display has it.
    Vsync,
};

/// The uncapped present modes to try, best first: mailbox, then immediate; immediate, then mailbox when `unpaced` (a
/// measurement wants nothing to wait). Vsync follows when neither is available, so it isn't listed.
[[nodiscard]] std::span<const PresentMode> PresentModeOrder(bool unpaced);

/// How game text is drawn: from the settings at start-up.
struct TextOptions {
    /// The language labels and captions show in.
    core::Language language = core::Language::English;
    /// Whether text has its dark outline.
    bool outline = true;
    /// Whether text casts its soft shadow.
    bool shadow = true;
    /// How much larger than its world size all game text is drawn, minimum sizes included: the text size setting.
    f32 scale = 1.0F;
};

/// Draws into the game's window with SDL_GPU, blending in linear space. The device, the window's
/// swapchain, the pipelines, the empty sprite atlas and the glyph cache are made when it's created; each scene's sprites
/// load into the atlas when the game needs them, and stay. Game text is drawn over the world from
/// distance fields: its size scales with the zoom, and its edges stay about a pixel wide.
class Renderer {
public:
    /// Creates a GPU device for `window` and presents to it, with the empty sprite atlas `manifest` describes, whose
    /// scenes load from `assets`, and the glyph cache, its font from `assets/fonts/`, or says why it couldn't. Texts show
    /// `strings`, drawn as `text` says, in `colours`; frames present as PresentModeOrder(`unpaced`) says; every glyph `strings`
    /// uses, in every language, is rasterised now (TextLayout::Prewarm()), so the game's own text never waits for one. The
    /// window must outlive the renderer.
    [[nodiscard]] static std::expected<Renderer, core::Error> Create(
        platform::Window& window,
        const std::filesystem::path& assets,
        std::shared_ptr<const core::Manifest> manifest,
        std::shared_ptr<const core::Strings> strings,
        const TextOptions& text,
        const core::UiColours& colours,
        bool unpaced
    );

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    /// Takes over `other`'s device, leaving `other` empty.
    Renderer(Renderer&& other) noexcept;
    Renderer& operator=(Renderer&& other) = delete;
    ~Renderer();

    /// Starts loading the sprites of `scene` that aren't in the atlas yet, decoding one per low-priority chunk on `jobs`,
    /// which must outlive the renderer. Does nothing when it already holds `scene`; no other scene may be loading.
    void BeginScene(core::SceneId scene, core::Jobs& jobs);

    /// Goes on with the load BeginScene() started: nothing while the sprites decode; then uploads them one at a time
    /// until `budgetNs` is spent (one at least), and once all are in, generates the mips. True once nothing is loading,
    /// the scene held; an error, ending the load, when a sprite failed to decode (`./dev atlas`) or upload.
    [[nodiscard]] std::expected<bool, core::Error> ContinueScene(u64 budgetNs);

    /// Acquires the window's next frame to draw into: waits while the GPU has its two frames in
    /// flight and, with vsync, for the display. The time for alpha is read after it; Draw()
    /// follows before the next Acquire(). A failure is logged, and the Draw() after it does nothing.
    void Acquire();
    /// Makes every Acquire() fail from now on without acquiring (`--fail-frames`), to see what the game does when the
    /// graphics card stops drawing.
    void FailAcquires();
    /// Ends the run of failed frames for a frame that can't be judged (a minimised window), logging that it ended without a
    /// verdict if any had failed: the next failure starts and logs a new run.
    void ForgetFailures();
    /// How many frames in a row have failed (0 once one is drawn, or after ForgetFailures()).
    [[nodiscard]] u64 FailedFrames() const;
    /// The first error of the current run of failed frames, as logged; empty message when none has failed.
    [[nodiscard]] const core::Error& FirstFailure() const;
    /// Draws `snapshot` into the frame Acquire() acquired as `view` shows it, and presents it. Uploads its instances only when
    /// it isn't the snapshot drawn last; a snapshot of a scene it doesn't hold draws the backdrop only. The seals on the
    /// ground, with their light, draw over its ground and decals (Snapshot::groundBatches), under the rest
    /// (gfx/seal_draw.hpp, at the render time, as SetSeals() describes them). The view's outlined instance gets a pale outline
    /// drawn over everything, and the snapshot's selected instance a gold one; the gold wins where both fall on one. Over them
    /// go the upright and overhead seals, capped in opacity, then the swords' trails and the overlay, placed every frame at the
    /// render time (gfx/volley_draw.hpp): the swords, each wearing its glow, the poison lance's bursts and the enemies' health
    /// bars, shown as their labels are. Over all of it go the snapshot's texts, then `extra` (the main thread's own, such as
    /// damage numbers), then `hud` in target pixels: its bars, then its lines. A glyph seen for the first time is rasterised on
    /// `jobs`, which must outlive the renderer, and drawn from the frame its field is uploaded: no frame waits for one.
    void Draw(
        const core::Snapshot& snapshot,
        const View& view,
        core::Jobs& jobs,
        std::span<const core::TextItem> extra = {},
        const HudDraw& hud = {}
    );

    /// Draws `text` from the next frame on.
    void SetTextOptions(const TextOptions& text);

    /// Draws labels, captions, the outlines of the hovered and selected entity, the ink of text's outlines and shadows,
    /// the health bars, the effects and the glows in `colours` from the next frame on, with no restart.
    void SetUiColours(const core::UiColours& colours);

    /// Draws the swords, bolts, bursts, health bars and glows as `effects` says from the next frame on, with no restart and
    /// no new timeline: the simulation never sees it.
    void SetEffects(const core::Effects& effects);

    /// Draws the seals in play as `seals` says from the next frame on, with no restart: their parts are packed now and
    /// uploaded with the next frame, never on a seal's first draw. Until it is called no seal is drawn.
    void SetSeals(const core::Seals& seals);

    /// Takes the names NPC labels are composed from (core::TextItem::npcName) and rasterises every glyph of the table now
    /// (TextLayout::PrewarmNames()), so a name never waits for one; a null table makes a named label say nothing. App
    /// gives it at the start and again after a data reload; the same table again changes nothing.
    void SetNames(std::shared_ptr<const core::Names> names);

    /// Draws the world map of a snapshot that shows it (core::Snapshot::worldMap) from `map` and `look` from the next
    /// frame on, with no restart: its painted ground, rivers and roads (along core::RoadCurves(), worked out here
    /// once), then the sprites of the tiles in view (gfx/terrain_draw.hpp) under a veil and the places' markers, all
    /// under the snapshot's ground and everything else; the sprites are laid out again only when the view reaches other
    /// tiles, the zoom changes or this is called again. Until it is called a snapshot's map draws nothing.
    /// `look`'s sprites must be resident when the map draws: in the manifest and in the sprites of the scene that shows the
    /// map (art/scenes.toml's `world`, data::WORLD_LOOK_SCENE), both of which data::LoadWorldLook checks; a look naming
    /// another sprite would draw from a page that isn't loaded.
    void SetWorld(std::shared_ptr<const core::WorldMap> map, const core::WorldLook& look);

    /// Draws the outline of the tile centred on `centre` (the target cursor) over the world map from the next frame on, none
    /// for no value; it draws over the fog's clouds, while the route in the snapshot (core::Snapshot::route,
    /// gfx/route_draw.hpp) draws between the map and the snapshot's ground. Nothing draws outside a snapshot that shows the
    /// map.
    void SetTargetTile(std::optional<core::Vec2> centre);

    /// How wide a HUD line saying `line` draws now, in target pixels at the text scale (HudLine's size). A glyph not
    /// rasterised yet counts nothing until it lands, a frame or two later.
    [[nodiscard]] f32 HudLineWidth(std::string_view line);

    /// Draws glows for the reduce flashing setting from the next frame on (off until it is called): pulses hold still and
    /// light glows dim.
    void SetReduceFlashing(bool reduce);

    /// Draws as Draw() does, but into a picture `width` by `height` pixels instead of the window, waits for the GPU and
    /// returns the picture, or says why it couldn't. Every glyph its texts and the HUD's lines need is rasterised on `jobs`
    /// first, waiting for them, so the picture never misses one. A picture stands alone: its labels and captions start at
    /// their targets, with no fade from a picture or frame before it. Stalls: for tests.
    [[nodiscard]] std::expected<Image, core::Error> Capture(
        const core::Snapshot& snapshot,
        const View& view,
        core::Jobs& jobs,
        u32 width,
        u32 height,
        std::span<const core::TextItem> extra = {},
        const HudDraw& hud = {}
    );

private:
    struct Impl;
    explicit Renderer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace gfx
