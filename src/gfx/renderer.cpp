#include "gfx/renderer.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"
#include "core/repeated_failure.hpp"
#include "gfx/internal/features/composite.hpp"
#include "gfx/internal/features/meshes.hpp"
#include "gfx/internal/features/ribbons.hpp"
#include "gfx/internal/features/sprites.hpp"
#include "gfx/internal/frame/frame.hpp"
#include "gfx/internal/gpu.hpp"
#include "gfx/internal/resources/scene_target.hpp"
#include "gfx/internal/resources/sprite_array.hpp"
#include "gfx/labels.hpp"
#include "gfx/sprite_block.hpp"
#include "gfx/text_layout.hpp"
#include "gfx/text_place.hpp"
#include "gfx/uploads.hpp"
#include "gfx/volley_draw.hpp"
#include "platform/clock.hpp"

#include <SDL3/SDL.h>

namespace gfx {

namespace {

#ifdef NDEBUG
constexpr bool DEBUG_MODE = false;
#else
constexpr bool DEBUG_MODE = true; // SDL_GPU's validation, in debug builds only (§8.9)
#endif

// Ink on dark paper, behind the scene where a window wider or taller than the reference shows past it.
// Linear values, which the composite encodes to about (0.08, 0.08, 0.09).
constexpr SDL_FColor BACKDROP{ .r = 0.0072F, .g = 0.0072F, .b = 0.0085F, .a = 1.0F };

// The outlines (design doc 0002.3 §3), three texels wide: the hovered entity's pale, the selected one's gold.
// Premultiplied RGBA8, red in the low byte.
constexpr f32 OUTLINE_TEXELS = 3.0F;

// A text's shadow falls this many base pixels down and right of it, scaling with the text.
constexpr f32 TEXT_SHADOW_BASE_PIXELS = 2.0F;
// Clip space spans this much across either axis.
constexpr f32 CLIP_SPAN = 2.0F;

std::string_view PresentModeName(SDL_GPUPresentMode mode) {
    if (mode == SDL_GPU_PRESENTMODE_MAILBOX) {
        return "mailbox";
    }
    return mode == SDL_GPU_PRESENTMODE_IMMEDIATE ? "immediate" : "vsync";
}

// Logs the first of a run of uploads that left `dropped` of `what` out, past the buffer's `limit`, and the first after
// such a run that fitted.
void ReportOverflow(core::RepeatedFailure& overflows, std::size_t dropped, std::string_view what, u32 limit) {
    PN_PROFILE_ZONE();
    if (dropped > 0) {
        if (overflows.Fail()) {
            PN_LOG_WARNING("{} {} past the buffer's {} aren't drawn", dropped, what, limit);
        }
    }
    else if (const u64 overflowed = overflows.Succeed(); overflowed > 0) {
        PN_LOG_INFO("the {} fit again, after {} uploads that didn't", what, overflowed);
    }
}

// The transform that places target pixels (x right, y down from the top left) on a target `width` by `height` pixels.
ClipTransform PixelsToClip(u32 width, u32 height) {
    return { .scaleX = CLIP_SPAN / static_cast<f32>(width),
             .scaleY = -CLIP_SPAN / static_cast<f32>(height),
             .offsetX = -1.0F,
             .offsetY = 1.0F };
}

// A glyph page: R8 distance fields, cleared to 0 (far outside any glyph) in a command buffer of its own, submitted at
// once, so the padding around its fields reads as nothing whatever happens to the frame that needed it.
std::expected<internal::Gpu<SDL_GPUTexture>, core::Error> CreateGlyphPage(SDL_GPUDevice* device) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET; // a target only to be cleared
    info.width = GLYPH_PAGE_SIZE;
    info.height = GLYPH_PAGE_SIZE;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.props = SDL_CreateProperties();
    SDL_SetStringProperty(info.props, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, "glyph page");
    internal::Gpu<SDL_GPUTexture> page{ SDL_CreateGPUTexture(device, &info), { device } };
    SDL_DestroyProperties(info.props);
    SDL_GPUCommandBuffer* commands = page == nullptr ? nullptr : SDL_AcquireGPUCommandBuffer(device);
    if (commands == nullptr) {
        return std::unexpected{ core::Error{ std::format("a glyph page wasn't created: {}", SDL_GetError()) } };
    }
    SDL_GPUColorTargetInfo target{};
    target.texture = page.get();
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_EndGPURenderPass(SDL_BeginGPURenderPass(commands, &target, 1, nullptr));
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        return std::unexpected{ core::Error{ std::format("a glyph page wasn't cleared: {}", SDL_GetError()) } };
    }
    return page;
}

// A scene loading (design doc 0002.3.1 §5): its sprites decode on the job system, then upload a few each frame.
struct SceneLoad {
    SceneLoad(
        core::SceneId loading,
        std::vector<core::atlas::SpriteId> missing,
        const std::filesystem::path& assets,
        const std::shared_ptr<const core::Manifest>& manifest,
        core::Jobs& jobs
    )
      : scene{ loading }
      , sprites{ std::move(missing) }
      , decoded(sprites.size())
      , decoding{ jobs.Start(
            static_cast<u32>(sprites.size()),
            [assets, manifest, names = std::span<const core::atlas::SpriteId>{ sprites }, slots = std::span{ decoded }](
                u32 chunk
            ) { slots[chunk] = internal::DecodeSprite(assets, manifest->Sprite(names[chunk])); }
        ) } {
    }

    core::SceneId scene;
    std::vector<core::atlas::SpriteId> sprites;                         // those not in the atlas when it began
    std::vector<std::expected<internal::Surface, core::Error>> decoded; // a slot per sprite
    std::size_t uploaded = 0;
    core::Jobs::Pending decoding; // last: destroyed first, waiting for the decodes before their slots go
};

} // namespace

struct Renderer::Impl {
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() = default;

    // What a frame or a capture shows, and on a target how large.
    struct Shown {
        const core::Snapshot* snapshot = nullptr;
        std::span<const core::TextItem> extra; // the main thread's texts, drawn after the snapshot's
        HudDraw hud;                           // drawn over everything, in target pixels
        const View* view = nullptr;
        u32 width = 0;
        u32 height = 0;
    };

    // Once a swapchain texture is acquired, the frame is submitted even on an error: SDL requires it.
    std::expected<void, core::Error> Frame(const internal::Frame& acquired, const Shown& shown, core::Jobs& jobs);
    // Uploads the snapshot's instances when they're new, and every frame the overlay's sprites and the trails (the
    // HUD's bars first, then the bursts, health bars and swords at the render time), the texts' quads (the HUD's lines
    // first, drawn last) and the glyph fields they need; then ends the glyph cache's frame. A recorded copy counts as
    // uploaded only once its commands are submitted (Submitted()): a dropped buffer must upload again.
    std::expected<void, core::Error> Upload(SDL_GPUCommandBuffer* commands, const Shown& shown, core::Jobs& jobs);
    // Makes the glyph pages the cache's fields are on.
    std::expected<void, core::Error> MakeGlyphPages();
    // What `item` says, in the language shown.
    std::string_view TextOf(const core::TextItem& item);
    // Lays out the texts into meshVertices and meshRuns, in target pixels, unrounded: captions, then the labels as the
    // label pass places them, then numbers (on `jobs`), so each draws over the one before.
    void PlaceTexts(const Shown& shown, core::Jobs& jobs);
    // The numbers that start from their creature's label (NumberOrigin), from this frame's labels as placed.
    void FindNumberOrigins();
    // Each quad of numberPages from meshVertices' quad `firstQuad` on, into meshRuns.
    void AddNumberRuns(u32 firstQuad);
    // `item`, laid out as `layout`, at its own anchor and size, its colour scaled by `opacity`.
    void PlaceText(const core::TextItem& item, const Layout& layout, f32 opacity);
    // `item`, a number running top to bottom, shaped and drawn at `pixels`, moved by its scatter and risen by its lift
    // from its anchor, or from its creature's NumberOrigin. Numbers across are composed instead (PlaceNumbers()).
    void PlaceNumber(const core::TextItem& item);
    // The label pass's input for `item`, a label, raised to the hovered or selected priority when it names those.
    LabelInput LabelOf(const core::TextItem& item, core::StableId hovered, core::StableId selected);
    // Each label's stackWidth: its name as a stack with as many digits as the frame's count can have; a label whose stack
    // text isn't complete isn't either.
    void MeasureStacks();
    // A label, `item`, showing `count` (its name, or the stack with that count) at `opacity` of its colour, its box's
    // bottom middle at `anchor`, drawn at `pixels`.
    void PlaceLabel(const core::TextItem& item, u32 count, f32 opacity, core::Vec2 anchor, f32 pixels);
    // `layout` drawn at `pixels` with its `align` point at `anchor`, in `colour`.
    void PlaceLayout(const Layout& layout, core::Vec2 anchor, core::TextAlign align, f32 pixels, u32 colour);
    // `layout` drawn at `pixels` with its box's top left at `topLeft`, in `colour`, its quads added to the last run when
    // they share its page and it is openRun or later.
    void PlaceAt(const Layout& layout, core::Vec2 topLeft, f32 pixels, u32 colour);
    // The HUD's lines, placed first, so the quads' room is theirs before the world's texts take any: the runs before
    // hudRuns, drawn after every other text.
    void PlaceHudLines(std::span<const HudLine> lines);
    std::expected<void, core::Error> UploadGlyphs(SDL_GPUCopyPass* copy);
    // The command buffer holding the last Upload() was submitted, or failed to be, or was cancelled.
    void Submitted(bool submitted);
    void Report(const std::expected<void, core::Error>& drawn);
    [[nodiscard]] bool Holds(core::SceneId scene) const;
    // The world pass into the scene target, fitted to `width` by `height`, then the final pass into `target`.
    std::expected<void, core::Error> DrawPasses(
        SDL_GPUCommandBuffer* commands,
        SDL_GPUTexture* target,
        u32 width,
        u32 height,
        const core::Snapshot& snapshot,
        const View& view
    );
    void DrawWorld(SDL_GPUCommandBuffer* commands, const core::Snapshot& snapshot, const View& view) const;
    void DrawFinal(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target) const;

    // Resources: the device and what lasts as long as it.
    SDL_Window* window = nullptr;
    internal::Device device = nullptr; // first: destroyed after everything it made
    internal::Gpu<SDL_Window> claim = nullptr;
    internal::SpriteArray sprites{};
    std::vector<internal::Gpu<SDL_GPUTexture>>
        glyphPages{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::unique_ptr<TextLayout> text = nullptr; // made by Create(): never null after
    std::shared_ptr<const core::Manifest> manifest = nullptr;
    std::shared_ptr<const core::Strings> strings = nullptr;
    TextOptions textOptions{};
    core::UiColours uiColours{};
    std::filesystem::path assets{}; // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::vector<bool> resident{};   // NOLINT(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::optional<SceneLoad> load = std::nullopt;
    // Features: what draws, each with its pipelines and buffers.
    internal::SpriteRenderer instances{};
    internal::MeshRenderer meshes{};
    internal::SpriteRenderer overlay{}; // the per-frame sprites: health bars, halos and swords (VolleyDrawing)
    internal::RibbonRenderer ribbons{};
    internal::Compositor compositor{};
    internal::SceneTarget sceneTarget{};
    // What every text of a frame shares, worked out once a frame by PlaceTexts(): the functions it calls for each text
    // (TextOf(), LabelOf(), PlaceText(), PlaceNumber(), PlaceLabel(), PlaceLayout()) read it and have no zones, as a
    // zone costs more than one text's work (src/AGENTS.md); PlaceTexts()' zone measures them.
    struct TextFrame {
        ClipTransform clip{};
        f32 alpha = 1.0F;
        u32 width = 0;
        u32 height = 0;
        f32 perUnit = 0.0F; // PixelsPerUnit()
        f32 labelPixels = 0.0F;
        f32 captionPixels = 0.0F;
        f32 numberPixels = 0.0F;
    };
    TextFrame textFrame{};
    // Per-frame state: the frame in flight, what its uploads hold, and the failures being counted.
    std::optional<internal::Frame> frame = std::nullopt; // acquired by Acquire(), submitted by Draw()
    UploadedSnapshot uploaded{};
    std::size_t glyphsRecorded = 0; // of the cache's pending fields, in the command buffer not yet submitted
    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer
    std::string number{};  // a number's text, made again for each
    std::string stacked{}; // a label stack's text, likewise
    LabelPass labels{};
    CaptionFades captionFades{};
    std::vector<LabelInput> labelInputs{};
    std::vector<NumberOrigin> numberOrigins{}; // by entity: where this frame's tiny creatures' numbers start
    std::vector<u32> numberPages{};            // the glyph page of each quad PlaceNumbers() placed this frame
    std::vector<std::size_t> numberQuads{};    // PlaceNumbers()' scratch: each chunk's first quad
    std::vector<MeshVertex> meshVertices{};
    std::vector<internal::MeshRun> meshRuns{};
    VolleyDrawing volleyDrawing{};     // this frame's health bars and swords, placed at the render time, and the HUD's bars
    std::size_t hudRuns = 0;           // meshRuns' first runs, which draw the HUD's lines
    std::size_t openRun = 0;           // the first of meshRuns a quad may join: none of the HUD's takes the world's texts
    std::vector<BarShown> barsShown{}; // by entity: how much of each creature's health bar shows, from its label
    core::RepeatedFailure failures{};
    core::RepeatedFailure overflows{};
    core::RepeatedFailure meshOverflows{};
    core::RepeatedFailure overlayOverflows{};
    core::RepeatedFailure ribbonOverflows{};
    core::RepeatedFailure swordOverflows{};
    core::RepeatedFailure uploadFailures{};
    // NOLINTEND(readability-redundant-member-init)
};

std::expected<void, core::Error> Renderer::Impl::Upload(SDL_GPUCommandBuffer* commands, const Shown& shown, core::Jobs& jobs) {
    PN_PROFILE_ZONE();
    const core::Snapshot& snapshot = *shown.snapshot;
    const SnapshotId shownId{ .epoch = snapshot.epoch, .wallTick = snapshot.wallTick };
    const bool newInstances = !uploaded.Holds(shownId) && Holds(snapshot.scene);
    std::expected<void, core::Error> glyphs = MakeGlyphPages();
    meshVertices.clear();
    meshRuns.clear();
    barsShown.clear();
    // The HUD first in both buffers, so an overflow drops the world's texts and effects, never it.
    openRun = 0;
    if (glyphs) {
        PlaceHudLines(shown.hud.lines);
    }
    hudRuns = meshRuns.size();
    openRun = hudRuns;
    if (glyphs) {
        PlaceTexts(shown, jobs);
    }
    volleyDrawing.Clear();
    if (Holds(snapshot.scene)) {
        const auto alpha = static_cast<f32>(shown.view->alpha);
        const f64 renderTick = core::RenderTick(snapshot.simulationTick, shown.view->alpha);
        DrawHudBars(shown.hud.bars, *manifest, volleyDrawing);
        DrawFlashes(snapshot.flashes, renderTick, *manifest, uiColours, volleyDrawing);
        DrawHealthBars(snapshot.healthBars, barsShown, alpha, *manifest, uiColours, volleyDrawing);
        DrawVolleys(
            snapshot.volleys, snapshot.volleyTargets, snapshot.simulationTick, alpha, *manifest, uiColours, jobs, volleyDrawing
        );
    }
    SDL_PushGPUDebugGroup(commands, "upload");
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    const std::expected<std::size_t, core::Error> dropped =
        newInstances ? instances.Upload(copy, snapshot.instances) : std::expected<std::size_t, core::Error>{ 0 };
    if (glyphs) {
        glyphs = UploadGlyphs(copy);
    }
    // Every frame: texts move with the camera and the render clock, unlike the instances.
    const std::expected<std::size_t, core::Error> quadsDropped =
        meshes.Upload(copy, glyphs ? std::span<const MeshVertex>{ meshVertices } : std::span<const MeshVertex>{});
    const std::expected<std::size_t, core::Error> overlayDropped = overlay.Upload(copy, volleyDrawing.instances);
    const std::expected<std::size_t, core::Error> ribbonsDropped = ribbons.Upload(copy, volleyDrawing.ribbons);
    SDL_EndGPUCopyPass(copy);
    SDL_PopGPUDebugGroup(commands);
    text->EndFrame(jobs);
    if (quadsDropped) {
        ReportOverflow(meshOverflows, *quadsDropped, "text quads", MAX_MESH_QUADS);
    }
    if (overlayDropped) {
        ReportOverflow(overlayOverflows, *overlayDropped, "overlay instances", MAX_OVERLAY_INSTANCES);
    }
    // Swords left out whole by DrawVolleys(), where the overlay's or the trails' room ran out, whichever is less.
    ReportOverflow(swordOverflows, volleyDrawing.droppedSwords, "flying swords", static_cast<u32>(MAX_DRAWN_SWORDS));
    if (ribbonsDropped) {
        ReportOverflow(ribbonOverflows, *ribbonsDropped, "ribbon quads", MAX_RIBBON_QUADS);
    }
    std::expected<void, core::Error> instancesCopied{};
    if (newInstances && dropped) {
        uploaded.Recorded(shownId);
        ReportOverflow(overflows, *dropped, "sprite instances", internal::MAX_INSTANCES);
    }
    else if (newInstances) {
        instancesCopied = std::unexpected{ dropped.error() }; // not marked uploaded: the next frame tries again
    }
    const auto copied = [](std::size_t /*dropped*/) {};
    return core::Combine(
        core::Combine(core::Combine(instancesCopied, glyphs), quadsDropped.transform(copied)),
        core::Combine(overlayDropped.transform(copied), ribbonsDropped.transform(copied))
    );
}

std::expected<void, core::Error> Renderer::Impl::MakeGlyphPages() {
    PN_PROFILE_ZONE();
    while (glyphPages.size() < text->Pages()) {
        std::expected<internal::Gpu<SDL_GPUTexture>, core::Error> made = CreateGlyphPage(device.get());
        if (!made) {
            return std::unexpected{ std::move(made.error()) };
        }
        glyphPages.push_back(std::move(*made));
    }
    return {};
}

std::string_view Renderer::Impl::TextOf(const core::TextItem& item) {
    if (item.kind != core::TextKind::Number) {
        return strings->Text(item.string, textOptions.language);
    }
    number.clear();
    std::format_to(std::back_inserter(number), "{}{}", item.sign == core::NumberSign::Plus ? "+" : "", item.value);
    return number;
}

void Renderer::Impl::PlaceTexts(const Shown& shown, core::Jobs& jobs) {
    PN_PROFILE_ZONE();
    const core::Snapshot& snapshot = *shown.snapshot;
    const auto idOf = [&snapshot](std::optional<std::size_t> instance) {
        return instance && *instance < snapshot.ids.size() ? snapshot.ids[*instance] : core::StableId{};
    };
    const core::StableId hovered = idOf(shown.view->outlined);
    const core::StableId selected = idOf(snapshot.selected);
    const f32 perUnit = PixelsPerUnit(shown.view->camera, shown.width, shown.height);
    textFrame = { .clip = WorldToClip(shown.view->camera, shown.width, shown.height),
                  .alpha = static_cast<f32>(shown.view->alpha),
                  .width = shown.width,
                  .height = shown.height,
                  .perUnit = perUnit,
                  .labelPixels = TextPixels(core::TextKind::Label, perUnit, textOptions.scale),
                  .captionPixels = TextPixels(core::TextKind::Caption, perUnit, textOptions.scale),
                  .numberPixels = TextPixels(core::TextKind::Number, perUnit, textOptions.scale) };
    const f32 captionOpacity = CaptionOpacity(textFrame.captionPixels, textOptions.scale);
    labelInputs.clear();
    captionFades.Begin(shown.view->frameClock);
    for (const std::span<const core::TextItem> texts : { std::span<const core::TextItem>{ snapshot.texts }, shown.extra }) {
        for (const core::TextItem& item : texts) {
            if (item.kind == core::TextKind::Caption) {
                const Layout& layout = text->Lay(TextOf(item), item.direction);
                PlaceText(item, layout, captionOpacity * captionFades.Opacity(item.string, layout.complete));
            }
            else if (item.kind == core::TextKind::Label) {
                labelInputs.push_back(LabelOf(item, hovered, selected));
            }
        }
    }
    MeasureStacks();
    // A label whose item has gone draws as the pass kept it, fading out.
    for (const PlacedLabel& placed :
         labels.Place(labelInputs, shown.width, shown.height, shown.view->frameClock, textOptions.scale)) {
        const core::TextItem& item = placed.label->item;
        const f32 pixels = placed.label->pixels;
        PlaceLabel(item, placed.previous, placed.opacity * (1.0F - placed.change), placed.anchor, pixels);
        PlaceLabel(item, placed.count, placed.opacity * placed.change, placed.anchor, pixels);
        barsShown.push_back({ .entity = placed.label->entity, .opacity = BarOpacity(placed) });
    }
    std::ranges::sort(barsShown, {}, [](const BarShown& bar) { return std::to_underlying(bar.entity); });
    FindNumberOrigins();
    const NumberPlacement placement{ .face = NumberFaceOf(*text),
                                     .clip = textFrame.clip,
                                     .alpha = textFrame.alpha,
                                     .width = textFrame.width,
                                     .height = textFrame.height,
                                     .pixels = textFrame.numberPixels,
                                     .origins = numberOrigins };
    const auto firstQuad = static_cast<u32>(meshVertices.size() / QUAD_VERTICES);
    numberPages.clear();
    const std::array<std::span<const core::TextItem>, 2> sources{ { snapshot.texts, shown.extra } };
    for (const std::span<const core::TextItem> texts : sources) {
        PlaceNumbers(texts, placement, jobs, meshVertices, numberPages, numberQuads);
    }
    AddNumberRuns(firstQuad);
    // No number runs down so far; one that does is shaped, after those across.
    for (const std::span<const core::TextItem> texts : sources) {
        for (const core::TextItem& item : texts) {
            if (item.kind == core::TextKind::Number && item.direction != core::TextDirection::LeftToRight) {
                PlaceNumber(item);
            }
        }
    }
}

void Renderer::Impl::FindNumberOrigins() {
    PN_PROFILE_ZONE();
    numberOrigins.clear();
    for (const LabelInput& label : labelInputs) {
        if (TooSmallToSee(label.entityPixels)) {
            // From the top of its label's box, so its numbers rise off the label rather than over its text.
            if (const std::optional<core::Vec2> bottom = labels.Anchor(label.entity)) {
                numberOrigins.push_back({ .entity = label.entity, .start = { .x = bottom->x, .y = bottom->y - label.height } });
            }
        }
    }
    std::ranges::sort(numberOrigins, {}, &NumberOrigin::entity);
}

void Renderer::Impl::AddNumberRuns(u32 firstQuad) {
    PN_PROFILE_ZONE();
    u32 quad = firstQuad;
    for (const u32 pageIndex : numberPages) {
        SDL_GPUTexture* const page = glyphPages.at(pageIndex).get();
        if (meshRuns.size() <= openRun || meshRuns.back().page != page) {
            meshRuns.push_back({ .page = page, .firstQuad = quad, .quads = 0 });
        }
        ++meshRuns.back().quads;
        ++quad;
    }
}

void Renderer::Impl::PlaceNumber(const core::TextItem& item) {
    if (item.opacity <= 0.0F) {
        return;
    }
    const f32 pixels = textFrame.numberPixels;
    const auto origin = std::ranges::lower_bound(numberOrigins, item.entity, {}, &NumberOrigin::entity);
    core::Vec2 start = origin != numberOrigins.end() && origin->entity == item.entity
                           ? origin->start
                           : TextAnchor(item, textFrame.clip, textFrame.alpha, textFrame.width, textFrame.height);
    start.x += item.scatter.x * pixels;
    start.y += (item.scatter.y - item.lift) * pixels;
    PlaceLayout(
        text->Lay(TextOf(item), item.direction),
        start,
        item.align,
        pixels,
        TextColour(item, uiColours, textOptions.outline, textOptions.shadow)
    );
}

void Renderer::Impl::MeasureStacks() {
    PN_PROFILE_ZONE();
    // A stack's count is below the number of labels, so the stack text with that many digits is as wide as any can be:
    // the font's digits are tabular (Noto Sans SC's '0' to '9' are all 555 units wide), so the digits' number decides.
    const auto most = static_cast<u32>(labelInputs.size());
    const std::string& stack = strings->Text(core::StringId::LabelStack, textOptions.language);
    struct Measured {
        f32 width = -1.0F; // unmeasured
        bool complete = false;
    };
    std::array<std::array<Measured, 2>, core::STRINGS> measured{};
    for (LabelInput& label : labelInputs) {
        const core::TextItem& item = label.item;
        Measured& stackText = measured.at(std::to_underlying(item.string)).at(std::to_underlying(item.direction));
        if (stackText.width < 0.0F) {
            StackText(stacked, stack, TextOf(item), most);
            const Layout& layout = text->Lay(stacked, item.direction);
            stackText = { .width = layout.width * (label.pixels / SDF_BASE_PIXELS), .complete = layout.complete };
        }
        label.stackWidth = stackText.width;
        label.complete = label.complete && stackText.complete;
    }
}

void Renderer::Impl::PlaceLabel(const core::TextItem& item, u32 count, f32 opacity, core::Vec2 anchor, f32 pixels) {
    if (opacity <= 0.0F) {
        return;
    }
    core::TextItem faded = item;
    faded.opacity *= opacity;
    std::string_view says = TextOf(item);
    if (count > 0) {
        StackText(stacked, strings->Text(core::StringId::LabelStack, textOptions.language), says, count);
        says = stacked;
    }
    PlaceLayout(
        text->Lay(says, item.direction),
        anchor,
        core::TextAlign::BottomCentre,
        pixels,
        TextColour(faded, uiColours, textOptions.outline, textOptions.shadow)
    );
}

LabelInput Renderer::Impl::LabelOf(const core::TextItem& item, core::StableId hovered, core::StableId selected) {
    const f32 perUnit = textFrame.perUnit;
    const f32 pixels = textFrame.labelPixels;
    const Layout& layout = text->Lay(TextOf(item), item.direction);
    const f32 scale = pixels / SDF_BASE_PIXELS;
    core::LabelPriority priority = item.priority;
    if (item.entity != core::StableId{} && item.entity == selected) {
        priority = core::LabelPriority::Selected;
    }
    else if (item.entity != core::StableId{} && item.entity == hovered) {
        priority = std::max(priority, core::LabelPriority::Hovered);
    }
    const f32 height = layout.height * scale;
    core::Vec2 anchor = TextAnchor(item, textFrame.clip, textFrame.alpha, textFrame.width, textFrame.height);
    if (item.align == core::TextAlign::TopCentre) {
        anchor.y += height; // the pass takes the middle of a box's bottom edge
    }
    return { .entity = item.entity,
             .nameKey = item.nameKey,
             .priority = priority,
             .anchor = anchor,
             .width = layout.width * scale,
             .height = height,
             .pixels = pixels,
             .entityPixels = item.height * perUnit,
             .complete = layout.complete,
             .item = item };
}

void Renderer::Impl::PlaceText(const core::TextItem& item, const Layout& layout, f32 opacity) {
    if (opacity <= 0.0F) {
        return;
    }
    core::TextItem faded = item;
    faded.opacity *= opacity;
    PlaceLayout(
        layout,
        TextAnchor(item, textFrame.clip, textFrame.alpha, textFrame.width, textFrame.height),
        item.align,
        TextPixels(item.kind, textFrame.perUnit, textOptions.scale),
        TextColour(faded, uiColours, textOptions.outline, textOptions.shadow)
    );
}

void Renderer::Impl::PlaceLayout(const Layout& layout, core::Vec2 anchor, core::TextAlign align, f32 pixels, u32 colour) {
    const f32 scale = pixels / SDF_BASE_PIXELS;
    PlaceAt(layout, TextTopLeft(anchor, { .x = layout.width * scale, .y = layout.height * scale }, align), pixels, colour);
}

void Renderer::Impl::PlaceAt(const Layout& layout, core::Vec2 topLeft, f32 pixels, u32 colour) {
    auto quad = static_cast<u32>(meshVertices.size() / QUAD_VERTICES);
    PlaceGlyphs(layout, topLeft, pixels, colour, meshVertices);
    for (const PlacedGlyph& glyph : layout.glyphs) {
        SDL_GPUTexture* const page = glyphPages.at(glyph.page).get();
        if (meshRuns.size() <= openRun || meshRuns.back().page != page) {
            meshRuns.push_back({ .page = page, .firstQuad = quad, .quads = 0 });
        }
        ++meshRuns.back().quads;
        ++quad;
    }
}

void Renderer::Impl::PlaceHudLines(std::span<const HudLine> lines) {
    PN_PROFILE_ZONE();
    const f32 pixels = HUD_PIXELS * textOptions.scale;
    for (const HudLine& line : lines) {
        PlaceAt(text->Lay(line.text, core::TextDirection::LeftToRight), line.topLeft, pixels, line.colour);
    }
}

std::expected<void, core::Error> Renderer::Impl::UploadGlyphs(SDL_GPUCopyPass* copy) {
    PN_PROFILE_ZONE();
    const std::span<const GlyphUpload> pending = text->Pending();
    if (pending.empty()) {
        return {};
    }
    std::size_t bytes = 0;
    for (const GlyphUpload& glyph : pending) {
        bytes += glyph.field.size();
    }
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = static_cast<u32>(bytes);
    // Released once recorded: SDL keeps it until the copy has run.
    const internal::Gpu<SDL_GPUTransferBuffer> transfer{ SDL_CreateGPUTransferBuffer(device.get(), &info), { device.get() } };
    void* mapped = transfer == nullptr ? nullptr : SDL_MapGPUTransferBuffer(device.get(), transfer.get(), false);
    if (mapped == nullptr) {
        return std::unexpected{ core::Error{
            std::format("no transfer buffer for {} new glyph fields: {}", pending.size(), SDL_GetError()) } };
    }
    const std::span<std::byte> staging = internal::BytesAt(mapped, bytes);
    std::size_t offset = 0;
    for (const GlyphUpload& glyph : pending) {
        std::ranges::copy(std::as_bytes(std::span{ glyph.field }), staging.subspan(offset).begin());
        offset += glyph.field.size();
    }
    SDL_UnmapGPUTransferBuffer(device.get(), transfer.get());
    offset = 0;
    for (const GlyphUpload& glyph : pending) {
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer.get();
        source.offset = static_cast<u32>(offset);
        source.pixels_per_row = glyph.width;
        source.rows_per_layer = glyph.height;
        SDL_GPUTextureRegion region{};
        region.texture = glyphPages.at(glyph.page).get();
        region.x = glyph.x;
        region.y = glyph.y;
        region.w = glyph.width;
        region.h = glyph.height;
        region.d = 1;
        SDL_UploadToGPUTexture(copy, &source, &region, false); // never cycled: the page's other fields stay
        offset += glyph.field.size();
    }
    glyphsRecorded = pending.size();
    return {};
}

void Renderer::Impl::Submitted(bool submitted) {
    PN_PROFILE_ZONE();
    uploaded.Submitted(submitted);
    if (submitted) {
        text->Uploaded(glyphsRecorded);
    }
    glyphsRecorded = 0;
}

std::expected<void, core::Error> Renderer::Impl::DrawPasses(
    SDL_GPUCommandBuffer* commands,
    SDL_GPUTexture* target,
    u32 width,
    u32 height,
    const core::Snapshot& snapshot,
    const View& view
) {
    PN_PROFILE_ZONE();
    if (std::expected<void, core::Error> fitted = internal::FitSceneTarget(device.get(), sceneTarget, width, height); !fitted) {
        return fitted;
    }
    DrawWorld(commands, snapshot, view);
    DrawFinal(commands, target);
    return {};
}

void Renderer::Impl::DrawFinal(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target) const {
    PN_PROFILE_ZONE();
    SDL_PushGPUDebugGroup(commands, "final");
    SDL_GPUColorTargetInfo info{};
    info.texture = target;
    info.load_op = SDL_GPU_LOADOP_DONT_CARE; // the composite covers every pixel
    info.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &info, 1, nullptr);
    compositor.Draw(pass, sceneTarget.texture.get());
    SDL_EndGPURenderPass(pass);
    SDL_PopGPUDebugGroup(commands);
}

void Renderer::Impl::DrawWorld(SDL_GPUCommandBuffer* commands, const core::Snapshot& snapshot, const View& view) const {
    PN_PROFILE_ZONE();
    const auto alpha = static_cast<f32>(view.alpha);
    const std::optional<std::size_t> outlined = view.outlined;
    SDL_PushGPUDebugGroup(commands, "world");
    SDL_GPUColorTargetInfo target{};
    target.texture = sceneTarget.texture.get();
    target.clear_color = BACKDROP;
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    target.cycle = true; // nothing loads the old contents (0002 §8.7)
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
    if (!Holds(snapshot.scene)) {
        SDL_EndGPURenderPass(pass);
        SDL_PopGPUDebugGroup(commands);
        return;
    }
    const ClipTransform transform = WorldToClip(view.camera, sceneTarget.width, sceneTarget.height);
    instances.Draw(commands, pass, snapshot.batches, sprites, transform, alpha);
    const f32 ring = OUTLINE_TEXELS / static_cast<f32>(manifest->pageSize);
    if (snapshot.selected) {
        instances.DrawOutline(
            commands,
            pass,
            { .instance = *snapshot.selected, .colour = uiColours.Of(core::UiColour::SelectedOutline), .ring = ring },
            sprites,
            transform,
            alpha
        );
    }
    // Hovering the selected entity keeps its gold: the selection is what a click changed.
    if (outlined && *outlined < snapshot.instances.size() && *outlined != snapshot.selected) {
        instances.DrawOutline(
            commands,
            pass,
            { .instance = static_cast<u32>(*outlined), .colour = uiColours.Of(core::UiColour::HoverOutline), .ring = ring },
            sprites,
            transform,
            alpha
        );
    }
    // The trails, then the overlay: the swords' tinted cores, their halos (additive), the lance's bursts (additive), the
    // health bars and the swords, at the render time; their instances hold no motion between ticks, so the overlay's alpha
    // changes nothing.
    ribbons.Draw(commands, pass, transform);
    overlay.Draw(commands, pass, volleyDrawing.batches, sprites, transform, alpha);
    // Game text over the world, its outlines and the overlay (the phase 6 chunk 3 spec), placed in target pixels; then the
    // HUD over all of it: its bars, then its lines.
    const f32 shadowShift = TEXT_SHADOW_BASE_PIXELS / static_cast<f32>(GLYPH_PAGE_SIZE); // a texel a base pixel
    const ClipTransform pixels = PixelsToClip(sceneTarget.width, sceneTarget.height);
    const internal::TextStyle style{ .outline = textOptions.outline ? 1.0F : 0.0F,
                                     .shadow = textOptions.shadow ? 1.0F : 0.0F,
                                     .shadowOffsetU = shadowShift,
                                     .shadowOffsetV = shadowShift,
                                     .ink = internal::InkOf(uiColours.Of(core::UiColour::TextInk)) };
    const std::span<const internal::MeshRun> runs{ meshRuns };
    const std::size_t hudRun = std::min(hudRuns, runs.size());
    meshes.Draw(commands, pass, runs.subspan(hudRun), pixels, style);
    overlay.Draw(commands, pass, volleyDrawing.hud, sprites, pixels, alpha);
    meshes.Draw(commands, pass, runs.first(hudRun), pixels, style);
    SDL_EndGPURenderPass(pass);
    SDL_PopGPUDebugGroup(commands);
}

std::expected<void, core::Error> Renderer::Impl::Frame(const internal::Frame& acquired, const Shown& shown, core::Jobs& jobs) {
    PN_PROFILE_ZONE();
    // A failed upload draws without what it held, but the frame is still submitted, as SDL requires.
    if (const std::expected<void, core::Error> copied = Upload(acquired.commands, shown, jobs); !copied) {
        if (uploadFailures.Fail()) {
            PN_LOG_ERROR("{}; what it held isn't drawn until an upload succeeds", copied.error().message);
        }
    }
    else if (const u64 failed = uploadFailures.Succeed(); failed > 0) {
        PN_LOG_INFO("uploads succeed again, after {} that failed", failed);
    }
    // Without a swapchain texture (the window can't be shown), the copy still runs and nothing is drawn (§8.3).
    std::expected<void, core::Error> drawn{};
    if (acquired.swapchain != nullptr) {
        drawn =
            DrawPasses(acquired.commands, acquired.swapchain, acquired.width, acquired.height, *shown.snapshot, *shown.view);
    }
    const std::expected<void, core::Error> submitted = internal::SubmitFrame(acquired);
    Submitted(submitted.has_value());
    return core::Combine(drawn, submitted);
}

void Renderer::Impl::Report(const std::expected<void, core::Error>& drawn) {
    PN_PROFILE_ZONE();
    if (!drawn) {
        if (failures.Fail()) {
            PN_LOG_ERROR("frames are failing: {}", drawn.error().message);
        }
    }
    else if (const u64 failed = failures.Succeed(); failed > 0) {
        PN_LOG_WARNING("frames are drawn again after {} failed", failed);
    }
}

bool Renderer::Impl::Holds(core::SceneId scene) const {
    PN_PROFILE_ZONE();
    return std::ranges::all_of(manifest->Scene(scene).sprites, [this](core::atlas::SpriteId sprite) {
        return resident.at(std::to_underlying(sprite));
    });
}

std::expected<Renderer, core::Error> Renderer::Create(
    platform::Window& window,
    const std::filesystem::path& assets,
    std::shared_ptr<const core::Manifest> manifest,
    std::shared_ptr<const core::Strings> strings,
    const TextOptions& text,
    const core::UiColours& colours
) {
    auto impl = std::make_unique<Impl>();
    impl->manifest = std::move(manifest);
    impl->strings = std::move(strings);
    impl->textOptions = text;
    impl->uiColours = colours;
    impl->assets = assets;
    impl->resident.resize(impl->manifest->sprites.size());
    impl->window = static_cast<SDL_Window*>(window.Handle());
    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
    SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
    SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
    SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, DEBUG_MODE);
    impl->device.reset(SDL_CreateGPUDeviceWithProperties(properties));
    SDL_DestroyProperties(properties);
    if (impl->device == nullptr) {
        return std::unexpected{ core::Error{ std::format("SDL_CreateGPUDeviceWithProperties failed: {}", SDL_GetError()) } };
    }
    SDL_GPUDevice* device = impl->device.get();
    if (!SDL_ClaimWindowForGPUDevice(device, impl->window)) {
        return std::unexpected{ core::Error{ std::format("SDL_ClaimWindowForGPUDevice failed: {}", SDL_GetError()) } };
    }
    impl->claim = internal::Gpu<SDL_Window>{ impl->window, { device } };

    // Rendering is uncapped (design doc 0002 §6): mailbox, then immediate; vsync is always there.
    SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
    for (const SDL_GPUPresentMode uncapped : { SDL_GPU_PRESENTMODE_MAILBOX, SDL_GPU_PRESENTMODE_IMMEDIATE }) {
        if (SDL_WindowSupportsGPUPresentMode(device, impl->window, uncapped)) {
            mode = uncapped;
            break;
        }
    }
    // Always plain SDR, which SDL guarantees: the composite encodes sRGB itself (design doc 0002.4.1 §3).
    if (!SDL_SetGPUSwapchainParameters(device, impl->window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode)) {
        return std::unexpected{ core::Error{ std::format("SDL_SetGPUSwapchainParameters failed: {}", SDL_GetError()) } };
    }
    PN_LOG_INFO(
        "GPU driver: {}, presenting with {}{}",
        SDL_GetGPUDeviceDriver(device),
        PresentModeName(mode),
        DEBUG_MODE ? ", debug mode" : ""
    );
    if (mode == SDL_GPU_PRESENTMODE_VSYNC) {
        PN_LOG_WARNING("neither mailbox nor immediate presentation is available, so frames follow the display");
    }

    // Pipelines are made at start-up (§8.8), so each backend checks the shaders when the game starts.
    std::expected<internal::SpriteRenderer, core::Error> instances =
        internal::SpriteRenderer::Create(device, internal::SCENE_FORMAT);
    if (!instances) {
        return std::unexpected{ std::move(instances.error()) };
    }
    impl->instances = std::move(*instances);
    std::expected<internal::MeshRenderer, core::Error> meshes = internal::MeshRenderer::Create(device, internal::SCENE_FORMAT);
    if (!meshes) {
        return std::unexpected{ std::move(meshes.error()) };
    }
    impl->meshes = std::move(*meshes);
    std::expected<internal::SpriteRenderer, core::Error> overlay =
        internal::SpriteRenderer::Create(device, internal::SCENE_FORMAT, MAX_OVERLAY_INSTANCES, "overlay instances");
    if (!overlay) {
        return std::unexpected{ std::move(overlay.error()) };
    }
    impl->overlay = std::move(*overlay);
    std::expected<internal::RibbonRenderer, core::Error> ribbons =
        internal::RibbonRenderer::Create(device, internal::SCENE_FORMAT);
    if (!ribbons) {
        return std::unexpected{ std::move(ribbons.error()) };
    }
    impl->ribbons = std::move(*ribbons);
    std::expected<internal::Compositor, core::Error> compositor =
        internal::Compositor::Create(device, SDL_GetGPUSwapchainTextureFormat(device, impl->window));
    if (!compositor) {
        return std::unexpected{ std::move(compositor.error()) };
    }
    impl->compositor = std::move(*compositor);
    std::expected<internal::SpriteArray, core::Error> sprites = internal::CreateSpriteArray(device, *impl->manifest);
    if (!sprites) {
        return std::unexpected{ std::move(sprites.error()) };
    }
    impl->sprites = std::move(*sprites);
    std::expected<TextLayout, core::Error> layout = TextLayout::Open(assets / "fonts");
    if (!layout) {
        return std::unexpected{ std::move(layout.error()) };
    }
    impl->text = std::make_unique<TextLayout>(std::move(*layout));
    impl->text->Prewarm(*impl->strings);
    return Renderer{ std::move(impl) };
}

Renderer::Renderer(std::unique_ptr<Impl> impl)
  : m_impl{ std::move(impl) } {
}
Renderer::Renderer(Renderer&& other) noexcept = default;
Renderer::~Renderer() = default;

void Renderer::BeginScene(core::SceneId scene, core::Jobs& jobs) {
    PN_ASSERT(!m_impl->load, "a scene began loading while another loads");
    std::vector<core::atlas::SpriteId> missing;
    for (const core::atlas::SpriteId sprite : m_impl->manifest->Scene(scene).sprites) {
        if (!m_impl->resident.at(std::to_underlying(sprite))) {
            missing.push_back(sprite);
        }
    }
    if (!missing.empty()) {
        m_impl->load.emplace(scene, std::move(missing), m_impl->assets, m_impl->manifest, jobs);
    }
}

std::expected<bool, core::Error> Renderer::ContinueScene(u64 budgetNs) {
    PN_PROFILE_ZONE();
    Impl& impl = *m_impl;
    if (!impl.load) {
        return true;
    }
    SceneLoad& load = *impl.load;
    if (!load.decoding.Done()) {
        return false;
    }
    const u64 startNs = platform::NowNs();
    const std::size_t first = load.uploaded;
    while (load.uploaded < load.sprites.size() && (load.uploaded == first || platform::NowNs() - startNs < budgetNs)) {
        const std::expected<internal::Surface, core::Error>& image = load.decoded.at(load.uploaded);
        const std::expected<void, core::Error> uploaded = image.and_then([&](const internal::Surface& surface) {
            return internal::UploadSprite(
                impl.device.get(),
                impl.sprites,
                impl.manifest->Sprite(load.sprites.at(load.uploaded)),
                impl.manifest->mipLevels,
                *surface
            );
        });
        if (!uploaded) {
            core::Error error = uploaded.error();
            impl.load.reset();
            return std::unexpected{ std::move(error) };
        }
        load.decoded.at(load.uploaded) = internal::Surface{}; // its pixels are on the GPU now
        ++load.uploaded;
    }
    if (load.uploaded < load.sprites.size()) {
        return false;
    }
    if (const std::expected<void, core::Error> mips = internal::GenerateMips(impl.device.get(), impl.sprites); !mips) {
        impl.load.reset();
        return std::unexpected{ mips.error() };
    }
    for (const core::atlas::SpriteId sprite : load.sprites) {
        impl.resident.at(std::to_underlying(sprite)) = true;
    }
    PN_LOG_DEBUG("{} sprites of scene {} decoded and uploaded", load.sprites.size(), impl.manifest->Scene(load.scene).name);
    impl.load.reset();
    return true;
}

void Renderer::Acquire() {
    PN_PROFILE_ZONE();
    PN_ASSERT(!m_impl->frame, "Acquire() twice without a Draw() between");
    std::expected<internal::Frame, core::Error> acquired = internal::AcquireFrame(m_impl->device.get(), m_impl->window);
    if (!acquired) {
        m_impl->Report(std::unexpected{ std::move(acquired.error()) });
        return;
    }
    m_impl->frame = *acquired;
}

void Renderer::Draw(
    const core::Snapshot& snapshot,
    const View& view,
    core::Jobs& jobs,
    std::span<const core::TextItem> extra,
    const HudDraw& hud
) {
    PN_PROFILE_ZONE();
    if (!m_impl->frame) {
        return; // the acquire failed, and said so
    }
    const internal::Frame acquired = *m_impl->frame;
    m_impl->frame.reset();
    m_impl->Report(m_impl->Frame(
        acquired,
        { .snapshot = &snapshot,
          .extra = extra,
          .hud = hud,
          .view = &view,
          .width = acquired.width,
          .height = acquired.height },
        jobs
    ));
}

void Renderer::SetTextOptions(const TextOptions& text) {
    m_impl->textOptions = text;
}

void Renderer::SetUiColours(const core::UiColours& colours) {
    m_impl->uiColours = colours;
}

std::expected<Image, core::Error> Renderer::Capture(
    const core::Snapshot& snapshot,
    const View& view,
    core::Jobs& jobs,
    u32 width,
    u32 height,
    std::span<const core::TextItem> extra,
    const HudDraw& hud
) {
    // Every glyph the texts need is asked for, then waited for: a picture never misses one.
    const auto ask = [this](const core::TextItem& item) {
        static_cast<void>(m_impl->text->Lay(m_impl->TextOf(item), item.direction));
    };
    std::ranges::for_each(snapshot.texts, ask);
    std::ranges::for_each(extra, ask);
    for (const HudLine& line : hud.lines) {
        static_cast<void>(m_impl->text->Lay(line.text, core::TextDirection::LeftToRight));
    }
    m_impl->text->Flush(jobs);
    // A picture stands alone: its labels and captions start at their targets, not fading from the picture before.
    m_impl->labels = LabelPass{};
    m_impl->captionFades = CaptionFades{};
    const Impl::Shown shown{
        .snapshot = &snapshot, .extra = extra, .hud = hud, .view = &view, .width = width, .height = height
    };
    SDL_GPUDevice* device = m_impl->device.get();
    // The composite's pipeline is made for the swapchain's format, so the picture is drawn in it too.
    const SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(device, m_impl->window);
    SDL_GPUTextureCreateInfo textureInfo{};
    textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
    textureInfo.format = format;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    textureInfo.width = width;
    textureInfo.height = height;
    textureInfo.layer_count_or_depth = 1;
    textureInfo.num_levels = 1;
    const internal::Gpu<SDL_GPUTexture> texture{ SDL_CreateGPUTexture(device, &textureInfo), { device } };
    const u32 bytes = width * height * static_cast<u32>(BYTES_PER_TEXEL);
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transferInfo.size = bytes;
    const internal::Gpu<SDL_GPUTransferBuffer> transfer{ SDL_CreateGPUTransferBuffer(device, &transferInfo), { device } };
    SDL_GPUCommandBuffer* commands = texture == nullptr || transfer == nullptr ? nullptr : SDL_AcquireGPUCommandBuffer(device);
    if (commands == nullptr) {
        return std::unexpected{ core::Error{ std::format("nothing to capture into: {}", SDL_GetError()) } };
    }
    return AfterUpload(
        m_impl->Upload(commands, shown, jobs),
        [this, commands] {
            SDL_CancelGPUCommandBuffer(commands); // allowed: no swapchain texture is acquired
            m_impl->Submitted(false);
        },
        [&]() -> std::expected<Image, core::Error> {
            if (std::expected<void, core::Error> drawn =
                    m_impl->DrawPasses(commands, texture.get(), width, height, snapshot, view);
                !drawn) {
                SDL_CancelGPUCommandBuffer(commands);
                m_impl->Submitted(false);
                return std::unexpected{ std::move(drawn.error()) };
            }
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
            SDL_GPUTextureRegion region{};
            region.texture = texture.get();
            region.w = width;
            region.h = height;
            region.d = 1;
            SDL_GPUTextureTransferInfo destination{};
            destination.transfer_buffer = transfer.get();
            SDL_DownloadFromGPUTexture(copy, &region, &destination);
            SDL_EndGPUCopyPass(copy);
            const bool submitted = SDL_SubmitGPUCommandBuffer(commands);
            m_impl->Submitted(submitted);
            if (!submitted) {
                return std::unexpected{ core::Error{ std::format("the capture wasn't submitted: {}", SDL_GetError()) } };
            }
            if (!SDL_WaitForGPUIdle(device)) {
                return std::unexpected{ core::Error{ std::format("the capture didn't finish: {}", SDL_GetError()) } };
            }
            void* mapped = SDL_MapGPUTransferBuffer(device, transfer.get(), false);
            if (mapped == nullptr) {
                return std::unexpected{ core::Error{ std::format("the capture wasn't read back: {}", SDL_GetError()) } };
            }
            const std::span<const std::byte> pixels = internal::BytesAt(mapped, bytes);
            Image image{ .width = width, .height = height, .rgba = { pixels.begin(), pixels.end() } };
            SDL_UnmapGPUTransferBuffer(device, transfer.get());
            if (format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM) {
                for (std::size_t blue = 0; blue + 2 < image.rgba.size(); blue += BYTES_PER_TEXEL) {
                    std::swap(image.rgba[blue], image.rgba[blue + 2]);
                }
            }
            return image;
        }
    );
}

} // namespace gfx
