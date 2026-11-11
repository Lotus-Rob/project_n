#pragma once

/// @file
/// @ingroup gfx
/// Drawing frames into the window with SDL_GPU.

#include "core/error.hpp"
#include "core/frame.hpp"
#include "core/jobs.hpp"
#include "core/manifest.hpp"
#include "core/strings.hpp"
#include "core/types.hpp"
#include "core/ui_colours.hpp"
#include "core/vec2.hpp"
#include "gfx/camera.hpp"
#include "gfx/image.hpp"
#include "platform/window.hpp"

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace gfx {

/// How game text is drawn (the phase 6 chunk 3 spec, "Settings" and "Accessibility"): from the settings at start-up.
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

/// How tall the HUD's text is drawn, in target pixels at text scale 1: TextOptions::scale multiplies it.
inline constexpr f32 HUD_PIXELS = 18.0F;

/// One line of the HUD's text, in target pixels: drawn after every other text, left to right, HUD_PIXELS tall times the
/// text scale.
struct HudLine {
    /// What it says; it must stay valid until the Draw() or Capture() it is passed to returns.
    std::string_view text;
    /// Its box's top-left corner, in target pixels from the target's top left.
    core::Vec2 topLeft{};
    /// Its colour, premultiplied and packed as core::UiColours' are.
    u32 colour = 0;
};

/// One of the HUD's bars, in target pixels: its background, and over it from its left a fill `fraction` of its length,
/// drawn over the world and its texts and under the HUD's text.
struct HudBar {
    /// Its top-left corner, in target pixels from the target's top left.
    core::Vec2 topLeft{};
    /// Its width and height, in target pixels.
    core::Vec2 size{};
    /// How much of it the fill covers, from 0 to 1 (clamped; NaN as 0).
    f32 fraction = 0.0F;
    /// The fill's colour, premultiplied and packed as core::UiColours' are.
    u32 fill = 0;
    /// The background's colour, likewise.
    u32 background = 0;
};

/// What the HUD draws in a frame, over everything else: its bars, then its lines.
struct HudDraw {
    /// Its lines of text.
    std::span<const HudLine> lines;
    /// Its bars.
    std::span<const HudBar> bars;
};

/// Draws into the game's window with SDL_GPU (design doc 0002 §8), blending in linear space (§8.4). The device, the window's
/// swapchain, the pipelines, the empty sprite atlas and the glyph cache are made when it's created; each scene's sprites
/// load into the atlas when the game needs them (design doc 0002.3.1 §5), and stay. Game text is drawn over the world from
/// distance fields (the phase 6 chunk 3 spec): its size scales with the zoom, and its edges stay about a pixel wide.
class Renderer {
public:
    /// Creates a GPU device for `window` and presents to it, with the empty sprite atlas `manifest` describes, whose
    /// scenes load from `assets`, and the glyph cache, its font from `assets/fonts/`, or says why it couldn't. Texts show
    /// `strings`, drawn as `text` says, in `colours`; every glyph `strings` uses, in every language, is rasterised now
    /// (TextLayout::Prewarm()), so the game's own text never waits for one. The window must outlive the renderer.
    [[nodiscard]] static std::expected<Renderer, core::Error> Create(
        platform::Window& window,
        const std::filesystem::path& assets,
        std::shared_ptr<const core::Manifest> manifest,
        std::shared_ptr<const core::Strings> strings,
        const TextOptions& text,
        const core::UiColours& colours
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
    /// flight and, with vsync, for the display. The time for alpha is read after it (§6); Draw()
    /// follows before the next Acquire(). A failure is logged, and the Draw() after it does nothing.
    void Acquire();
    /// Draws `snapshot` into the frame Acquire() acquired as `view` shows it, and presents it. Uploads its instances only when
    /// it isn't the snapshot drawn last (§8.3); a snapshot of a scene it doesn't hold draws the backdrop only.
    /// The view's outlined instance gets a pale outline drawn over everything, and the snapshot's
    /// selected instance a gold one; the gold wins where both fall on one. Over them go the swords' trails and the
    /// overlay, placed every frame at the render time (gfx/volley_draw.hpp): the swords' tinted cores and halos, the poison
    /// lance's bursts, the enemies' health bars, shown as their labels are, and the swords. Over all of it go the snapshot's
    /// texts, then `extra` (the main thread's own, such as damage numbers), then `hud` in target pixels: its bars, then its
    /// lines. A glyph seen for the first time is rasterised on `jobs`, which must outlive the renderer, and drawn from the
    /// frame its field is uploaded: no frame waits for one.
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
    /// the health bars and the effects in `colours` from the next frame on, with no restart.
    void SetUiColours(const core::UiColours& colours);

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
