#include "gfx/renderer.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"
#include "core/repeated_failure.hpp"
#include "core/tick_clock.hpp"
#include "gfx/cloud_draw.hpp"
#include "gfx/glow_table.hpp"
#include "gfx/ground.hpp"
#include "gfx/internal/features/composite.hpp"
#include "gfx/internal/features/ground.hpp"
#include "gfx/internal/features/meshes.hpp"
#include "gfx/internal/features/ribbons.hpp"
#include "gfx/internal/features/rivers.hpp"
#include "gfx/internal/features/seals.hpp"
#include "gfx/internal/features/sprites.hpp"
#include "gfx/internal/frame/frame.hpp"
#include "gfx/internal/gpu.hpp"
#include "gfx/internal/resources/scene_target.hpp"
#include "gfx/internal/resources/sprite_array.hpp"
#include "gfx/labels.hpp"
#include "gfx/route_draw.hpp"
#include "gfx/seal_draw.hpp"
#include "gfx/sprite_block.hpp"
#include "gfx/terrain_draw.hpp"
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
constexpr bool DEBUG_MODE = true; // SDL_GPU's validation, in debug builds only
#endif

/// Ink on dark paper, behind the scene where a window wider or taller than the reference shows past it.
/// Linear values, which the composite encodes to about (0.08, 0.08, 0.09).
constexpr SDL_FColor BACKDROP{ .r = 0.0072F, .g = 0.0072F, .b = 0.0085F, .a = 1.0F };

/// A text's shadow falls this many base pixels down and right of it, scaling with the text.
constexpr f32 TEXT_SHADOW_BASE_PIXELS = 2.0F;
/// Clip space spans this much across either axis.
constexpr f32 CLIP_SPAN = 2.0F;

std::string_view PresentModeName(SDL_GPUPresentMode mode) {
    if (mode == SDL_GPU_PRESENTMODE_MAILBOX) {
        return "mailbox";
    }
    return mode == SDL_GPU_PRESENTMODE_IMMEDIATE ? "immediate" : "vsync";
}

/// Logs the first of a run of uploads that left `dropped` of `what` out, past the buffer's `limit`, and the first after
/// such a run that fitted.
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

/// Logs, once until they draw again, seals DrawSeals() left out for a size too small to lay out: `tiny` this frame.
void ReportTinySeals(core::RepeatedFailure& failures, std::size_t tiny) {
    PN_PROFILE_ZONE();
    if (tiny > 0) {
        if (failures.Fail()) {
            PN_LOG_WARNING("{} seals too small to lay out (a size that isn't a normal float) aren't drawn", tiny);
        }
    }
    else if (const u64 failed = failures.Succeed(); failed > 0) {
        PN_LOG_INFO("every seal is large enough to draw again, after {} frames that weren't", failed);
    }
}

/// The transform that places target pixels (x right, y down from the top left) on a target `width` by `height` pixels.
ClipTransform PixelsToClip(u32 width, u32 height) {
    return { .scaleX = CLIP_SPAN / static_cast<f32>(width),
             .scaleY = -CLIP_SPAN / static_cast<f32>(height),
             .offsetX = -1.0F,
             .offsetY = 1.0F };
}

/// A glyph page: R8 distance fields, cleared to 0 (far outside any glyph) in a command buffer of its own, submitted at
/// once, so the padding around its fields reads as nothing whatever happens to the frame that needed it.
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

/// A scene loading: its sprites decode on the job system, then upload a few each frame.
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
    std::vector<core::atlas::SpriteId> sprites;                               // those not in the atlas when it began
    std::vector<std::expected<internal::DecodedSprite, core::Error>> decoded; // a slot per sprite
    u64 transformNs = 0;                                                      // the fields' total time, summed as they upload
    std::size_t uploaded = 0;
    core::Jobs::Pending decoding; // last: destroyed first, waiting for the decodes before their slots go
};

/// How wide a label stack of one string is laid out, once measured in a frame, in base pixels (a label scales it by its
/// own pixel size: a place name and a creature's label may share a string).
struct MeasuredStack {
    f32 width = -1.0F; // unmeasured
    bool complete = false;
};

/// A label stack's widths in a frame, at each count it is measured at.
struct StackWidths {
    MeasuredStack widest;  // at the frame's widest count
    MeasuredStack earlier; // at the widest count before it changed
};

/// A waiting label's index in the frame's label inputs while it hasn't gone in.
constexpr std::size_t NOT_IN = std::numeric_limits<std::size_t>::max();

/// Which count a label's stack text is measured at.
enum class StackCount : u8 {
    Widest,  ///< the frame's widest (WidestCount() of its label count): what a stack's room is kept for
    Earlier, ///< the widest before it last changed: stands in, from the cache only, while the frame's is laid out
};

/// Where a text not yet measured this frame gets its layout.
enum class Laying : u8 {
    CachedOnly, ///< only from the cache, complete, never made: a label laid out at once, without spending the budget
    InBudget,   ///< TextLayout::TryLay(), within the frame's budget: a label's turn
};

/// `item`'s label priority, raised to the hovered or selected priority when it names those.
core::LabelPriority PriorityOf(const core::TextItem& item, core::StableId hovered, core::StableId selected) {
    if (item.entity != core::StableId{} && item.entity == selected) {
        return core::LabelPriority::Selected;
    }
    if (item.entity != core::StableId{} && item.entity == hovered) {
        return std::max(item.priority, core::LabelPriority::Hovered);
    }
    return item.priority;
}

} // namespace

struct Renderer::Impl {
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() = default;

    /// What a frame or a capture shows, and on a target how large.
    struct Shown {
        const core::Snapshot* snapshot = nullptr;
        std::span<const core::TextItem> extra; // the main thread's texts, drawn after the snapshot's
        HudDraw hud;                           // drawn over everything, in target pixels
        const View* view = nullptr;
        u32 width = 0;
        u32 height = 0;
    };

    /// Once a swapchain texture is acquired, the frame is submitted even on an error: SDL requires it.
    std::expected<void, core::Error> Frame(const internal::Frame& acquired, const Shown& shown, core::Jobs& jobs);
    /// Uploads the snapshot's instances when they're new, and every frame the overlay's sprites and the trails (the
    /// HUD's bars first, then the bursts, health bars and swords at the render time), the texts' quads (the HUD's lines
    /// first, drawn last) and the glyph fields they need; then ends the glyph cache's frame. A recorded copy counts as
    /// uploaded only once its commands are submitted (Submitted()): a dropped buffer must upload again.
    std::expected<void, core::Error> Upload(SDL_GPUCommandBuffer* commands, const Shown& shown, core::Jobs& jobs);
    /// Lays out and copies the world map's sprites (DrawTerrain()) for the world `shown` shows at its zoom, when it shows the
    /// map and they differ from what the buffer holds; returns how many instances didn't fit.
    std::expected<std::size_t, core::Error> UploadTerrain(SDL_GPUCopyPass* copy, const Shown& shown);
    /// Builds and copies the world map's ground field and rivers when the map or the look changed, and works out the part
    /// of the view the ground covers and its uniforms, when `shown` shows the map; nothing of either draws otherwise.
    std::expected<void, core::Error> UploadGround(SDL_GPUCopyPass* copy, const Shown& shown);
    /// Creates the world map's ground and river pipelines on `gpu`, or says why it couldn't.
    std::expected<void, core::Error> CreateGround(SDL_GPUDevice* gpu);
    /// The world the camera shows, as a rectangle, for the window's size.
    static core::Rect ViewRect(const Shown& shown);
    /// Lays out the route and the cursor tile for the frame and copies each into its buffer, none when neither shows; the
    /// route's overflow, if any.
    std::expected<std::size_t, core::Error> UploadRoute(SDL_GPUCopyPass* copy, const Shown& shown);
    /// Lays out the fog's clouds for the view and the frame clock and copies them, none without fog on the world map; the
    /// buffer's overflow, if any.
    std::expected<std::size_t, core::Error> UploadClouds(SDL_GPUCopyPass* copy, const Shown& shown);
    /// Lays out the snapshot's seals at the render time into sealDrawing, its showcase seals only when `showcase`, and logs
    /// once each seal key the table lacks among those drawn.
    void LaySeals(const core::Snapshot& snapshot, f64 renderTick, bool showcase);
    /// Makes the glyph pages the cache's fields are on.
    std::expected<void, core::Error> MakeGlyphPages();
    /// What `item` says, in the language shown.
    std::string_view TextOf(const core::TextItem& item);
    /// Lays out the texts into meshVertices and meshRuns, in target pixels, unrounded: captions, then the labels as the
    /// label pass places them, then numbers (on `jobs`), so each draws over the one before.
    void PlaceTexts(const Shown& shown, core::Jobs& jobs);
    /// The numbers that start from their creature's label (NumberOrigin), from this frame's labels as placed.
    void FindNumberOrigins();
    /// Each quad of numberPages from meshVertices' quad `firstQuad` on, into meshRuns.
    void AddNumberRuns(u32 firstQuad);
    /// `item`, laid out as `layout`, at its own anchor and size, its colour scaled by `opacity`.
    void PlaceText(const core::TextItem& item, const Layout& layout, f32 opacity);
    /// `item`, a number running top to bottom, shaped and drawn at `pixels`, moved by its scatter and risen by its lift
    /// from its anchor, or from its creature's NumberOrigin. Numbers across are composed instead (PlaceNumbers()).
    void PlaceNumber(const core::TextItem& item);
    /// Puts labelItems into labelInputs. One too small to read goes in only when its name is cached, and measures no
    /// stack text: it covers no room. A readable one whose name and stack text are cached and complete goes in at once;
    /// so does one whose stack text at the widest count isn't but at the earlier count is, which then waits its turn to
    /// lay out the new text. The others wait and take their turns (OrderWaiting()) laying out their name, then their
    /// stack text, within the frame's layout budget, until it is spent: that label and every later one stay out this
    /// frame (or keep the earlier stack width).
    void GatherLabels(core::StableId hovered, core::StableId selected);
    /// GatherLabels()' turns: the waiting labels in OrderWaiting()'s order, each laying out what it lacks within the
    /// frame's layout budget, until it is spent.
    void TakeTurns();
    /// The label pass's input for `item`, a label at `priority` whose anchor point is at `anchor` (TextAnchor()), laid out
    /// as `layout`, its stack text measured as `stack`; without one it keeps no room for a stack.
    [[nodiscard]] LabelInput LabelOf(
        const core::TextItem& item,
        core::LabelPriority priority,
        core::Vec2 anchor,
        const Layout& layout,
        std::optional<MeasuredStack> stack
    ) const;
    /// The pixel size `item`, a label or a place's name, is drawn at this frame.
    [[nodiscard]] f32 LabelPixels(const core::TextItem& item) const {
        return item.kind == core::TextKind::Place ? textFrame.placePixels : textFrame.labelPixels;
    }
    /// How wide `item`'s name is as a stack counting `count` (stackCount or earlierStackCount): measured once a frame per
    /// name, direction and count, then kept, laid out as `laying` says the first time; none when that gives no layout.
    [[nodiscard]] std::optional<MeasuredStack> StackOf(const core::TextItem& item, StackCount count, Laying laying);
    /// A label, `item`, showing `count` (its name, or the stack with that count) at `opacity` of its colour, its box's
    /// bottom middle at `anchor`, drawn at `pixels`.
    void PlaceLabel(const core::TextItem& item, u32 count, f32 opacity, core::Vec2 anchor, f32 pixels);
    /// `layout` drawn at `pixels` with its `align` point at `anchor`, in `colour`.
    void PlaceLayout(const Layout& layout, core::Vec2 anchor, core::TextAlign align, f32 pixels, u32 colour);
    /// `layout` drawn at `pixels` with its box's top left at `topLeft`, in `colour`, its quads added to the last run when
    /// they share its page and it is openRun or later.
    void PlaceAt(const Layout& layout, core::Vec2 topLeft, f32 pixels, u32 colour);
    /// The HUD's lines, placed first, so the quads' room is theirs before the world's texts take any: the runs before
    /// hudRuns, drawn after every other text.
    void PlaceHudLines(std::span<const HudLine> lines);
    /// How wide a HUD line laid out as `layout` draws, in target pixels at the text scale.
    [[nodiscard]] f32 HudWidth(const Layout& layout) const {
        return layout.width * (HUD_PIXELS * textOptions.scale / SDF_BASE_PIXELS);
    }
    std::expected<void, core::Error> UploadGlyphs(SDL_GPUCopyPass* copy);
    /// The command buffer holding the last Upload() was submitted, or failed to be, or was cancelled.
    void Submitted(bool submitted);
    void Report(const std::expected<void, core::Error>& drawn);
    [[nodiscard]] bool Holds(core::SceneId scene) const;
    /// The world pass into the scene target, fitted to `width` by `height`, then the final pass into `target`.
    std::expected<void, core::Error> DrawPasses(
        SDL_GPUCommandBuffer* commands,
        SDL_GPUTexture* target,
        u32 width,
        u32 height,
        const core::Snapshot& snapshot,
        const View& view
    );
    void DrawWorld(SDL_GPUCommandBuffer* commands, const core::Snapshot& snapshot, const View& view) const;
    /// What every glow of the frame showing `snapshot` as `view` shows it shares.
    [[nodiscard]] internal::GlowFrame GlowFrameOf(const core::Snapshot& snapshot, const View& view) const;
    /// The glow table changed (new effects or palette): the next Upload() copies it again.
    void GlowsChanged();
    void DrawFinal(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target) const;

    /// Resources: the device and what lasts as long as it.
    SDL_Window* window = nullptr;
    internal::Device device = nullptr; // first: destroyed after everything it made
    internal::Gpu<SDL_Window> claim = nullptr;
    internal::SpriteArray sprites{};
    std::vector<internal::Gpu<SDL_GPUTexture>> glyphPages;
    std::unique_ptr<TextLayout> text = nullptr; // made by Create(): never null after
    std::shared_ptr<const core::Manifest> manifest = nullptr;
    std::shared_ptr<const core::Strings> strings = nullptr;
    std::shared_ptr<const core::Names> names = nullptr; // null until SetNames(): a named label says nothing
    TextOptions textOptions{};
    core::UiColours uiColours{};
    core::Effects effects{};
    core::Seals sealLooks{};                                  // empty until SetSeals(): no seal is drawn
    std::shared_ptr<const core::WorldMap> worldMap = nullptr; // null until SetWorld(): no map is drawn
    core::WorldLook worldLook{};
    std::filesystem::path assets;
    std::vector<bool> resident;
    std::optional<SceneLoad> load = std::nullopt;
    /// Features: what draws, each with its pipelines and buffers.
    internal::SpriteRenderer instances{};
    internal::MeshRenderer meshes{};
    internal::GroundRenderer ground{};  // the world map's painted ground, under everything on it
    internal::RiverRenderer rivers{};   // the world map's rivers, over the ground
    internal::SpriteRenderer terrain{}; // the world map's roads, crossings, stamps and markers (TerrainDrawing)
    internal::SpriteRenderer route{};   // the player's route (TerrainDrawing), over the map
    internal::SpriteRenderer clouds{};  // the fog's clouds (CloudDrawing), over the world's sprites
    internal::SpriteRenderer cursor{};  // the cursor's tile (TerrainDrawing), over the clouds
    internal::SpriteRenderer overlay{}; // the per-frame sprites: swords, bursts and health bars (VolleyDrawing)
    internal::GlowTableBuffer glowTable{};
    internal::FogTexture fog{}; // the world map's fog, which every sprite draw is shaded by
    internal::RibbonRenderer ribbons{};
    internal::SealRenderer seals{}; // with the table's pack
    internal::Compositor compositor{};
    internal::SceneTarget sceneTarget{};
    /// What every text of a frame shares, worked out once a frame by PlaceTexts(): the functions it calls for each text
    /// (TextOf(), LabelOf(), LabelPixels(), StackOf(), PlaceText(), PlaceNumber(), PlaceLabel(), PlaceLayout()) read it
    /// and have no zones, as a zone costs more than one text's work (src/AGENTS.md); PlaceTexts()' zone measures them.
    struct TextFrame {
        ClipTransform clip{};
        f32 alpha = 1.0F;
        u32 width = 0;
        u32 height = 0;
        f32 perUnit = 0.0F; // PixelsPerUnit()
        f32 labelPixels = 0.0F;
        f32 placePixels = 0.0F;
        f32 captionPixels = 0.0F;
        f32 numberPixels = 0.0F;
        FogFrame fog{}; // the view's: labels fade by it (FogOpacity())
    };
    TextFrame textFrame{};
    /// Per-frame state: the frame in flight, what its uploads hold, and the failures being counted.
    std::optional<internal::Frame> frame = std::nullopt; // acquired by Acquire(), submitted by Draw()
    UploadedSnapshot uploaded{};
    /// The glow table's versions: the latest (effects and palette), the one on the GPU, and one recorded but not yet submitted.
    u64 glowVersion = 0;
    u64 glowHeld = 0;
    std::optional<u64> glowRecorded = std::nullopt;
    bool reduceFlashing = false;
    std::size_t glyphsRecorded = 0; // of the cache's pending fields, in the command buffer not yet submitted
    std::string number;             // a number's text, made again for each
    NameTexts nameTexts;            // the NPC names this frame's labels show, each composed once
    std::unordered_map<u64, StackWidths> measuredNames; // a named label's stack widths per NameKey and direction
    std::vector<std::array<StackWidths, 2>> measured;   // a label stack's widths per string and direction, likewise
    std::string stacked;                                // a label stack's text, likewise
    u32 stackCount = 0;                                 // the widest count of the last frame that had labels (WidestCount())
    u32 earlierStackCount = 0;                          // the widest count before stackCount changed to what it is
    LabelPass labels;
    CaptionFades captionFades;
    std::vector<const core::TextItem*> labelItems; // the frame's labels and place names, in the snapshot's order
    std::vector<WaitingLabel> waitingLabels;       // the labels in labelItems not laid out yet, in their turns
    std::vector<std::size_t> waitingInputs;        // per label in labelItems: its index in labelInputs while it waits
    std::vector<LabelInput> labelInputs;
    std::vector<NumberOrigin> numberOrigins; // by entity: where this frame's tiny creatures' numbers start
    std::vector<u32> numberPages;            // the glyph page of each quad PlaceNumbers() placed this frame
    std::vector<std::size_t> numberQuads;    // PlaceNumbers()' scratch: each chunk's first quad
    std::vector<MeshVertex> meshVertices;
    std::vector<internal::MeshRun> meshRuns;
    /// What the terrain's buffer holds is what TerrainDrawing laid out for the tiles `span` reaches at `zoom` (stamps thin
    /// out with it) and the map and look `version` counts (SetWorld()): laid out and copied again only when that changes.
    /// Held once its commands are submitted.
    struct TerrainKey {
        core::TileSpan span;
        f32 zoom = 1.0F;
        u64 version = 0;
        [[nodiscard]] bool operator==(const TerrainKey& other) const {
            return version == other.version && std::bit_cast<u32>(zoom) == std::bit_cast<u32>(other.zoom) &&
                   span.firstColumn == other.span.firstColumn && span.lastColumn == other.span.lastColumn &&
                   span.firstRow == other.span.firstRow && span.lastRow == other.span.lastRow;
        }
    };
    TerrainDrawing terrainDrawing;                        // the last layout, which the buffer holds when terrainShown
    std::optional<TerrainKey> terrainLaid = std::nullopt; // what terrainDrawing was laid out for
    std::optional<TerrainKey> terrainHeld = std::nullopt;
    std::optional<TerrainKey> terrainRecorded = std::nullopt;
    u64 worldVersion = 0;
    TerrainDrawing routeDrawing;  // this frame's route, laid out again each frame; held when routeShown
    TerrainDrawing targetDrawing; // this frame's cursor tile, laid out again each frame; held when targetShown
    CloudDrawing cloudDrawing;    // this frame's clouds, laid out again each frame; held when cloudsShown
    bool targetShown = false;     // whether this frame draws the cursor's tile
    bool cloudsShown = false;     // whether this frame draws the clouds
    std::optional<core::Vec2> targetTile = std::nullopt; // SetTargetTile(): the cursor tile's centre, if it shows
    bool routeShown = false;                             // whether this frame draws the route buffer: it holds something
    bool terrainShown = false; // whether this frame draws the terrain: the snapshot shows the map and its buffer is current
    std::optional<core::Rect> groundQuad = std::nullopt; // the part of this frame's view the ground and rivers cover, if any
    GroundUniforms groundUniforms{};                     // what this frame's ground is painted by
    GroundUniforms veilUniforms{};                       // what this frame's veil is painted by
    VolleyDrawing volleyDrawing; // this frame's health bars and swords, placed at the render time, and the HUD's bars
    SealDrawing sealDrawing;     // this frame's seals, laid out at the render time
    std::vector<core::SealKey> sealsMissing; // the seal keys the table lacked, each logged once
    std::size_t hudRuns = 0;                 // meshRuns' first runs, which draw the HUD's lines
    std::size_t openRun = 0;                 // the first of meshRuns a quad may join: none of the HUD's takes the world's texts
    std::vector<BarShown> barsShown;         // by entity: how much of each creature's health bar shows, from its label
    core::RepeatedFailure failures;
    core::RepeatedFailure overflows;
    core::RepeatedFailure meshOverflows;
    core::RepeatedFailure overlayOverflows;
    core::RepeatedFailure terrainOverflows;
    core::RepeatedFailure routeOverflows;
    core::RepeatedFailure cloudOverflows;
    core::RepeatedFailure ribbonOverflows;
    core::RepeatedFailure swordOverflows;
    core::RepeatedFailure sealOverflows;
    core::RepeatedFailure tinySealFailures;
    core::RepeatedFailure sealQuadOverflows;
    core::RepeatedFailure sealParticleOverflows;
    core::RepeatedFailure uploadFailures;
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
    sealDrawing.Clear();
    if (Holds(snapshot.scene)) {
        const auto alpha = static_cast<f32>(shown.view->alpha);
        const f64 renderTick = core::RenderTick(snapshot.simulationTick, shown.view->alpha);
        LaySeals(snapshot, renderTick, shown.view->showcase);
        DrawHudBars(shown.hud.bars, *manifest, volleyDrawing);
        DrawFlashes(snapshot.flashes, renderTick, *manifest, uiColours, effects, volleyDrawing);
        DrawHealthBars(snapshot.healthBars, barsShown, alpha, *manifest, uiColours, effects, volleyDrawing);
        DrawVolleys(
            snapshot.volleys,
            snapshot.volleyTargets,
            snapshot.simulationTick,
            alpha,
            *manifest,
            uiColours,
            effects,
            jobs,
            volleyDrawing
        );
    }
    SDL_PushGPUDebugGroup(commands, "upload");
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    const std::expected<std::size_t, core::Error> dropped =
        newInstances ? instances.Upload(copy, snapshot.instances) : std::expected<std::size_t, core::Error>{ 0 };
    std::expected<void, core::Error> glowsCopied{};
    if (glowHeld != glowVersion) {
        glowsCopied = glowTable.Upload(copy, BuildGlowTable(effects, uiColours));
        if (glowsCopied) {
            glowRecorded = glowVersion;
        }
    }
    if (glyphs) {
        glyphs = UploadGlyphs(copy);
    }
    // Every frame: texts move with the camera and the render clock, unlike the instances.
    const std::expected<std::size_t, core::Error> quadsDropped =
        meshes.Upload(copy, glyphs ? std::span<const MeshVertex>{ meshVertices } : std::span<const MeshVertex>{});
    const std::expected<void, core::Error> groundCopied = UploadGround(copy, shown);
    const std::expected<std::size_t, core::Error> terrainDropped = UploadTerrain(copy, shown);
    const std::expected<std::size_t, core::Error> routeDropped = UploadRoute(copy, shown);
    const std::expected<std::size_t, core::Error> cloudsDropped = UploadClouds(copy, shown);
    const std::expected<std::size_t, core::Error> overlayDropped = overlay.Upload(copy, volleyDrawing.instances);
    const std::expected<std::size_t, core::Error> ribbonsDropped = ribbons.Upload(copy, volleyDrawing.ribbons);
    const std::expected<std::size_t, core::Error> sealQuadsDropped = seals.Upload(copy, sealDrawing);
    const std::expected<void, core::Error> fogCopied = fog.Update(copy, shown.view->fog, worldLook.fog);
    SDL_EndGPUCopyPass(copy);
    SDL_PopGPUDebugGroup(commands);
    text->EndFrame(jobs, shown.view->frameClock);
    if (quadsDropped) {
        ReportOverflow(meshOverflows, *quadsDropped, "text quads", MAX_MESH_QUADS);
    }
    if (terrainDropped) {
        ReportOverflow(terrainOverflows, *terrainDropped, "terrain instances", MAX_TERRAIN_INSTANCES);
    }
    if (routeDropped) {
        ReportOverflow(routeOverflows, *routeDropped, "route instances", MAX_ROUTE_INSTANCES);
    }
    if (cloudsDropped) {
        ReportOverflow(cloudOverflows, *cloudsDropped, "cloud instances", MAX_CLOUD_INSTANCES);
    }
    if (overlayDropped) {
        ReportOverflow(overlayOverflows, *overlayDropped, "overlay instances", MAX_OVERLAY_INSTANCES);
    }
    // Swords left out whole by DrawVolleys(), where the overlay's or the trails' room ran out, whichever is less.
    ReportOverflow(swordOverflows, volleyDrawing.droppedSwords, "flying swords", static_cast<u32>(MAX_DRAWN_SWORDS));
    if (ribbonsDropped) {
        ReportOverflow(ribbonOverflows, *ribbonsDropped, "ribbon quads", MAX_RIBBON_QUADS);
    }
    // Seals left out whole by DrawSeals(), past the frame's room, particles past a seal's, and quads past the buffer's.
    ReportOverflow(sealOverflows, sealDrawing.droppedSeals, "seals", static_cast<u32>(MAX_SEALS_DRAWN));
    ReportTinySeals(tinySealFailures, sealDrawing.tinySeals);
    ReportOverflow(sealParticleOverflows, sealDrawing.droppedParticles, "seal particles", core::MAX_SEAL_PARTICLES);
    if (sealQuadsDropped) {
        ReportOverflow(sealQuadOverflows, *sealQuadsDropped, "seal quads", MAX_SEAL_QUADS);
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
        core::Combine(
            core::Combine(core::Combine(core::Combine(instancesCopied, glowsCopied), glyphs), quadsDropped.transform(copied)),
            core::Combine(fogCopied, groundCopied)
        ),
        core::Combine(
            core::Combine(overlayDropped.transform(copied), ribbonsDropped.transform(copied)),
            core::Combine(
                sealQuadsDropped.transform(copied),
                core::Combine(
                    core::Combine(terrainDropped.transform(copied), routeDropped.transform(copied)),
                    cloudsDropped.transform(copied)
                )
            )
        )
    );
}

core::Rect Renderer::Impl::ViewRect(const Shown& shown) {
    const ClipTransform transform = WorldToClip(shown.view->camera, std::max(shown.width, 1U), std::max(shown.height, 1U));
    return { .min = ClipToWorld(transform, { .x = -1.0F, .y = 1.0F }),
             .max = ClipToWorld(transform, { .x = 1.0F, .y = -1.0F }) };
}

std::expected<std::size_t, core::Error> Renderer::Impl::UploadTerrain(SDL_GPUCopyPass* copy, const Shown& shown) {
    PN_PROFILE_ZONE();
    terrainShown = false;
    if (!shown.snapshot->worldMap || worldMap == nullptr || !Holds(shown.snapshot->scene)) {
        return std::size_t{ 0 };
    }
    // The world the camera shows, whole tiles of it: nothing culls ahead, so the map never lags the camera.
    const core::Rect view = ViewRect(shown);
    const f32 zoom = shown.view->camera.zoom;
    const TerrainKey key{ .span = worldMap->TilesIn(view), .zoom = zoom, .version = worldVersion };
    if (terrainHeld == key && terrainLaid == key) {
        terrainShown = true;
        return std::size_t{ 0 };
    }
    DrawTerrain(*worldMap, worldLook, *manifest, view, zoom, terrainDrawing);
    terrainLaid = key;
    const std::expected<std::size_t, core::Error> dropped = terrain.Upload(copy, terrainDrawing.instances);
    if (dropped) {
        terrainRecorded = key;
        terrainShown = true;
    }
    return dropped;
}

std::expected<void, core::Error> Renderer::Impl::UploadGround(SDL_GPUCopyPass* copy, const Shown& shown) {
    PN_PROFILE_ZONE();
    groundQuad.reset();
    if (!shown.snapshot->worldMap || worldMap == nullptr || !Holds(shown.snapshot->scene)) {
        return {};
    }
    // SetWorld() counts each new map or look, which is all the field and the rivers are built from.
    const std::expected<void, core::Error> field = ground.Update(copy, *worldMap, worldLook, worldVersion);
    const std::expected<void, core::Error> laid = rivers.Update(copy, *worldMap, worldLook, worldVersion);
    groundQuad = GroundQuad(ViewRect(shown), *worldMap);
    const bool paperOnly = shown.view->terrainParts == TerrainParts::PaperOnly;
    groundUniforms = GroundUniformsOf(*worldMap, worldLook, paperOnly ? GROUND_MODE_PAPER : GROUND_MODE_GROUND);
    groundUniforms.zoom = shown.view->camera.zoom;
    veilUniforms = groundUniforms;
    veilUniforms.mode = GROUND_MODE_VEIL;
    return core::Combine(field, laid);
}

std::expected<void, core::Error> Renderer::Impl::CreateGround(SDL_GPUDevice* gpu) {
    std::expected<internal::GroundRenderer, core::Error> madeGround =
        internal::GroundRenderer::Create(gpu, internal::SCENE_FORMAT);
    if (!madeGround) {
        return std::unexpected{ std::move(madeGround.error()) };
    }
    ground = std::move(*madeGround);
    std::expected<internal::RiverRenderer, core::Error> madeRivers =
        internal::RiverRenderer::Create(gpu, internal::SCENE_FORMAT);
    if (!madeRivers) {
        return std::unexpected{ std::move(madeRivers.error()) };
    }
    rivers = std::move(*madeRivers);
    return {};
}

std::expected<std::size_t, core::Error> Renderer::Impl::UploadRoute(SDL_GPUCopyPass* copy, const Shown& shown) {
    PN_PROFILE_ZONE();
    routeShown = false;
    targetShown = false;
    if (!shown.snapshot->worldMap || worldMap == nullptr || !Holds(shown.snapshot->scene)) {
        return std::size_t{ 0 };
    }
    routeDrawing.instances.clear();
    routeDrawing.batches.clear();
    DrawRoute(shown.snapshot->route, worldLook.route, *manifest, routeDrawing);
    targetDrawing.instances.clear();
    targetDrawing.batches.clear();
    DrawTargetTile(targetTile, worldLook.route, *manifest, targetDrawing);
    if (!targetDrawing.instances.empty()) {
        // One instance in a buffer of one: it can't overflow, only fail.
        const std::expected<std::size_t, core::Error> placed = cursor.Upload(copy, targetDrawing.instances);
        targetShown = placed.has_value();
        if (!placed) {
            return placed;
        }
    }
    if (routeDrawing.instances.empty()) {
        return std::size_t{ 0 };
    }
    const std::expected<std::size_t, core::Error> dropped = route.Upload(copy, routeDrawing.instances);
    routeShown = dropped.has_value();
    return dropped;
}

std::expected<std::size_t, core::Error> Renderer::Impl::UploadClouds(SDL_GPUCopyPass* copy, const Shown& shown) {
    PN_PROFILE_ZONE();
    cloudsShown = false;
    if (!shown.snapshot->worldMap || worldMap == nullptr || !Holds(shown.snapshot->scene) || shown.view->fog.columns == 0) {
        return std::size_t{ 0 };
    }
    const core::Rect view = ViewRect(shown);
    DrawClouds(shown.view->fog, worldLook.fog, *manifest, view, shown.view->camera.zoom, shown.view->frameClock, cloudDrawing);
    if (cloudDrawing.instances.empty()) {
        return std::size_t{ 0 };
    }
    const std::expected<std::size_t, core::Error> dropped = clouds.Upload(copy, cloudDrawing.instances);
    cloudsShown = dropped.has_value();
    return dropped.transform([&](std::size_t left) { return left + cloudDrawing.dropped; });
}

void Renderer::Impl::LaySeals(const core::Snapshot& snapshot, f64 renderTick, bool showcase) {
    PN_PROFILE_ZONE();
    for (const core::Seal& seal : snapshot.seals) {
        if ((showcase || !seal.showcase) && sealLooks.Find(seal.key) == nullptr &&
            !std::ranges::contains(sealsMissing, seal.key)) {
            sealsMissing.push_back(seal.key);
            PN_LOG_WARNING("a seal (key {:#018x}) isn't in seals.yaml, so it draws nothing", seal.key);
        }
    }
    // The render clock's whole tick: a seal is in play from its start tick to the tick before its end.
    const u64 wholeTick = renderTick > 0.0 ? static_cast<u64>(std::floor(renderTick)) : 0;
    DrawSeals(
        snapshot.seals,
        sealLooks,
        seals.Pack(),
        uiColours,
        renderTick / static_cast<f64>(core::TICKS_PER_SECOND),
        wholeTick,
        showcase,
        sealDrawing
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
    if (item.npcName) {
        return nameTexts.Of(names.get(), *item.npcName, textOptions.language);
    }
    if (item.kind != core::TextKind::Number) {
        return strings->Text(item.string, textOptions.language);
    }
    number.clear();
    std::format_to(std::back_inserter(number), "{}{}", item.sign == core::NumberSign::Plus ? "+" : "", item.value);
    return number;
}

void Renderer::Impl::PlaceTexts(const Shown& shown, core::Jobs& jobs) {
    PN_PROFILE_ZONE();
    nameTexts.Clear(); // a name is composed once a frame at most, in the language the frame shows
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
                  .placePixels = TextPixels(core::TextKind::Place, perUnit, textOptions.scale),
                  .captionPixels = TextPixels(core::TextKind::Caption, perUnit, textOptions.scale),
                  .numberPixels = TextPixels(core::TextKind::Number, perUnit, textOptions.scale),
                  .fog = shown.view->fog };
    const f32 captionOpacity = CaptionOpacity(textFrame.captionPixels, textOptions.scale);
    labelItems.clear();
    captionFades.Begin(shown.view->frameClock);
    for (const std::span<const core::TextItem> texts : { std::span<const core::TextItem>{ snapshot.texts }, shown.extra }) {
        for (const core::TextItem& item : texts) {
            if (item.kind == core::TextKind::Caption) {
                const Layout& layout = text->Lay(TextOf(item), item.direction);
                PlaceText(item, layout, captionOpacity * captionFades.Opacity(item.string, layout.complete));
            }
            else if (item.kind == core::TextKind::Label || item.kind == core::TextKind::Place) {
                labelItems.push_back(&item);
            }
        }
    }
    GatherLabels(hovered, selected);
    // A label whose item has gone draws as the pass kept it, fading out.
    for (const PlacedLabel& placed :
         labels.Place(labelInputs, shown.width, shown.height, shown.view->frameClock, textOptions.scale)) {
        const core::TextItem& item = placed.label->item;
        const f32 pixels = placed.label->pixels;
        const f32 opacity = placed.opacity * FogOpacity(item, textFrame.fog, textFrame.alpha);
        PlaceLabel(item, placed.previous, opacity * (1.0F - placed.change), placed.anchor, pixels);
        PlaceLabel(item, placed.count, opacity * placed.change, placed.anchor, pixels);
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

void Renderer::Impl::GatherLabels(core::StableId hovered, core::StableId selected) {
    PN_PROFILE_ZONE();
    // A stack's count is below the number of labels, so the stack text with that many digits is as wide as any can be:
    // the font's digits are tabular (Noto Sans SC's '0' to '9' are all 555 units wide), so the digits' number decides.
    // Measuring the widest count of that length keeps the text, and so its cached layout, the same while the label
    // count changes by a few each frame. When it gains or loses a digit, every stack text is new at once: the earlier
    // count's stands in until each label's turn lays the new one out, so labels on the screen never wait for theirs.
    if (const u32 widest = WidestCount(static_cast<u32>(labelItems.size())); widest != 0 && widest != stackCount) {
        earlierStackCount = stackCount;
        stackCount = widest;
    }
    measured.assign(strings->text.size(), {});
    measuredNames.clear();
    labelInputs.clear();
    waitingLabels.clear();
    waitingInputs.assign(labelItems.size(), NOT_IN);
    for (std::size_t index = 0; index < labelItems.size(); ++index) {
        const core::TextItem& item = *labelItems[index];
        // One the fog hides wholly would still take room and win overlaps against labels that show.
        if (FogOpacity(item, textFrame.fog, textFrame.alpha) <= 0.0F) {
            continue;
        }
        const core::LabelPriority priority = PriorityOf(item, hovered, selected);
        const core::Vec2 anchor = TextAnchor(item, textFrame.clip, textFrame.alpha, textFrame.width, textFrame.height);
        const Layout* name = text->Cached(TextOf(item), item.direction);
        if (!Readable(LabelPixels(item), textOptions.scale)) {
            // It only fades out, so it isn't worth a turn of the budget, and needs no stack text.
            if (name != nullptr) {
                labelInputs.push_back(LabelOf(item, priority, anchor, *name, std::nullopt));
            }
            continue;
        }
        // A cached layout missing a glyph waits, as its turn makes it again once the glyph has landed.
        const bool named = name != nullptr && name->complete;
        if (const std::optional<MeasuredStack> stack =
                named ? StackOf(item, StackCount::Widest, Laying::CachedOnly) : std::nullopt) {
            labelInputs.push_back(LabelOf(item, priority, anchor, *name, stack));
            continue;
        }
        if (const std::optional<MeasuredStack> earlier =
                named && earlierStackCount != 0 ? StackOf(item, StackCount::Earlier, Laying::CachedOnly) : std::nullopt) {
            waitingInputs[index] = labelInputs.size();
            labelInputs.push_back(LabelOf(item, priority, anchor, *name, earlier));
        }
        waitingLabels.push_back({ .item = index, .entity = item.entity, .priority = priority, .anchor = anchor });
    }
    TakeTurns();
}

void Renderer::Impl::TakeTurns() {
    constexpr f32 HALF = 0.5F;
    OrderWaiting(
        waitingLabels, { .x = static_cast<f32>(textFrame.width) * HALF, .y = static_cast<f32>(textFrame.height) * HALF }
    );
    for (const WaitingLabel& waiting : waitingLabels) {
        const core::TextItem& item = *labelItems[waiting.item];
        if (const std::size_t input = waitingInputs[waiting.item]; input != NOT_IN) {
            const std::optional<MeasuredStack> stack = StackOf(item, StackCount::Widest, Laying::InBudget);
            if (!stack) {
                break; // the budget is spent: the rest keep their turns for the frames after
            }
            // One still missing a glyph would fade the label out; the earlier width serves until it is complete.
            if (stack->complete) {
                LabelInput& label = labelInputs[input];
                label.stackWidth = stack->width * (label.pixels / SDF_BASE_PIXELS);
            }
            continue;
        }
        const Layout* name = text->TryLay(TextOf(item), item.direction);
        const std::optional<MeasuredStack> stack =
            name != nullptr ? StackOf(item, StackCount::Widest, Laying::InBudget) : std::nullopt;
        if (!stack) {
            break;
        }
        labelInputs.push_back(LabelOf(item, waiting.priority, waiting.anchor, *name, stack));
    }
}

std::optional<MeasuredStack> Renderer::Impl::StackOf(const core::TextItem& item, StackCount count, Laying laying) {
    StackWidths& widths = item.npcName
                              ? measuredNames[(u64{ core::NameKey(*item.npcName) } << 1U) | std::to_underlying(item.direction)]
                              : measured.at(std::to_underlying(item.string)).at(std::to_underlying(item.direction));
    MeasuredStack& stackText = count == StackCount::Widest ? widths.widest : widths.earlier;
    if (stackText.width < 0.0F) {
        StackText(
            stacked,
            strings->Text(core::StringId::LabelStack, textOptions.language),
            TextOf(item),
            count == StackCount::Widest ? stackCount : earlierStackCount
        );
        const Layout* layout =
            laying == Laying::InBudget ? text->TryLay(stacked, item.direction) : text->Cached(stacked, item.direction);
        if (layout == nullptr || (laying == Laying::CachedOnly && !layout->complete)) {
            return std::nullopt;
        }
        stackText = { .width = layout->width, .complete = layout->complete };
    }
    return stackText;
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

LabelInput Renderer::Impl::LabelOf(
    const core::TextItem& item,
    core::LabelPriority priority,
    core::Vec2 anchor,
    const Layout& layout,
    std::optional<MeasuredStack> stack
) const {
    const f32 pixels = LabelPixels(item);
    const f32 scale = pixels / SDF_BASE_PIXELS;
    const f32 height = layout.height * scale;
    if (item.align == core::TextAlign::TopCentre) {
        anchor.y += height; // the pass takes the middle of a box's bottom edge
    }
    return { .entity = item.entity,
             .nameKey = LabelKey(item),
             .priority = priority,
             .anchor = anchor,
             .width = layout.width * scale,
             .height = height,
             .stackWidth = stack ? stack->width * scale : 0.0F,
             .pixels = pixels,
             .entityPixels = item.height * textFrame.perUnit,
             .complete = layout.complete && (!stack || stack->complete),
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
        const Layout& layout = text->Lay(line.text, core::TextDirection::LeftToRight);
        core::Vec2 topLeft = line.topLeft;
        if (line.alignRight) {
            topLeft.x -= HudWidth(layout);
        }
        PlaceAt(layout, topLeft, pixels, line.colour);
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
    seals.Submitted(submitted);
    fog.Submitted(submitted);
    ground.Submitted(submitted);
    rivers.Submitted(submitted);
    if (submitted && terrainRecorded) {
        terrainHeld = terrainRecorded;
    }
    terrainRecorded.reset();
    if (submitted) {
        text->Uploaded(glyphsRecorded);
        glowHeld = glowRecorded.value_or(glowHeld);
    }
    glowRecorded.reset();
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
    target.cycle = true; // nothing loads the old contents
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
    if (!Holds(snapshot.scene)) {
        SDL_EndGPURenderPass(pass);
        SDL_PopGPUDebugGroup(commands);
        return;
    }
    const ClipTransform transform = WorldToClip(view.camera, sceneTarget.width, sceneTarget.height);
    const internal::SealTarget sealTarget{ .worldToClip = transform, .width = sceneTarget.width, .height = sceneTarget.height };
    const internal::GlowFrame glows = GlowFrameOf(snapshot, view);
    const std::span<const core::Batch> batches{ snapshot.batches };
    const std::size_t groundBatches = std::min<std::size_t>(snapshot.groundBatches, batches.size());
    // The map under all of it: it is the ground of a scene that shows it, which publishes none. Its painted ground first,
    // then the rivers on it, then the roads, crossings, stamps and paintings, the veil's haze over them all, and the places'
    // markers above the haze so every place reads; none of it where the view misses the map. Plain paper alone, its
    // groundUniforms in the paper mode, when the view asks for nothing else.
    const bool paperOnly = view.terrainParts == TerrainParts::PaperOnly;
    if (groundQuad) {
        ground.Draw(commands, pass, transform, *groundQuad, groundUniforms, fog);
    }
    if (groundQuad && !paperOnly) {
        rivers.Draw(commands, pass, transform, fog);
    }
    const std::span<const core::Batch> terrainBatches{ terrainDrawing.batches };
    const std::size_t underVeil = std::min<std::size_t>(terrainDrawing.markersFrom, terrainBatches.size());
    const std::size_t roads =
        view.terrainParts == TerrainParts::AllButRoads ? std::min<std::size_t>(terrainDrawing.roadBatches, underVeil) : 0;
    if (terrainShown && !paperOnly) {
        terrain.Draw(commands, pass, terrainBatches.first(underVeil).subspan(roads), sprites, transform, alpha, glows, fog);
    }
    if (groundQuad && !paperOnly) {
        ground.Draw(commands, pass, transform, *groundQuad, veilUniforms, fog);
    }
    if (terrainShown && !paperOnly) {
        terrain.Draw(commands, pass, terrainBatches.subspan(underVeil), sprites, transform, alpha, glows, fog);
    }
    // Over the map, under the ground and everything on it: the line is the map's, not a thing standing on it.
    if (routeShown) {
        route.Draw(commands, pass, routeDrawing.batches, sprites, transform, alpha, glows, fog);
    }
    instances.Draw(commands, pass, batches.first(groundBatches), sprites, transform, alpha, glows, fog);
    seals.DrawGround(commands, pass, sealTarget, sprites);
    instances.Draw(commands, pass, batches.subspan(groundBatches), sprites, transform, alpha, glows, fog);
    if (snapshot.selected) {
        instances.DrawGlowOnly(
            commands, pass, *snapshot.selected, core::GlowRole::Selected, sprites, transform, alpha, glows, fog
        );
    }
    // Hovering the selected entity keeps its gold: the selection is what a click changed.
    if (outlined && *outlined < snapshot.instances.size() && *outlined != snapshot.selected) {
        instances.DrawGlowOnly(
            commands, pass, static_cast<u32>(*outlined), core::GlowRole::Hover, sprites, transform, alpha, glows, fog
        );
    }
    // The clouds hide what is unexplored, so they come after the things standing on the map, and the cursor's tile after
    // them: the player always sees where the click will go.
    if (cloudsShown) {
        clouds.Draw(commands, pass, cloudDrawing.batches, sprites, transform, alpha, glows, fog);
    }
    if (targetShown) {
        cursor.Draw(commands, pass, targetDrawing.batches, sprites, transform, alpha, glows, fog);
    }
    // The upright and overhead seals over the creatures, capped in opacity, under all that follows: the swords fly out over
    // them, and the health bars, damage numbers and HUD stay readable.
    seals.DrawAbove(commands, pass, sealTarget, sprites);
    // The overlay's instances hold no motion between ticks, so its alpha changes nothing.
    ribbons.Draw(commands, pass, transform);
    overlay.Draw(commands, pass, volleyDrawing.batches, sprites, transform, alpha, glows, fog);
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
    overlay.Draw(commands, pass, volleyDrawing.hud, sprites, pixels, alpha, glows, fog);
    meshes.Draw(commands, pass, runs.first(hudRun), pixels, style);
    SDL_EndGPURenderPass(pass);
    SDL_PopGPUDebugGroup(commands);
}

internal::GlowFrame Renderer::Impl::GlowFrameOf(const core::Snapshot& snapshot, const View& view) const {
    PN_PROFILE_ZONE();
    const GlowTime time = GlowTimeAt(snapshot.simulationTick, view.alpha);
    internal::GlowFrame glows{ .table = glowTable.Buffer(),
                               .tick = time.tick,
                               .fraction = time.fraction,
                               .zoom = view.camera.zoom,
                               .reduceFlashing = reduceFlashing,
                               .texelUv = 1.0F / static_cast<f32>(manifest->pageSize) };
    for (std::size_t element = 0; element < core::HIT_KINDS; ++element) {
        glows.elementColours.at(element) = uiColours.Of(core::EffectColour(static_cast<core::HitKind>(element)));
    }
    return glows;
}

void Renderer::Impl::GlowsChanged() {
    ++glowVersion;
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
    // Without a swapchain texture (the window can't be shown), the copy still runs and nothing is drawn.
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

std::span<const PresentMode> PresentModeOrder(bool unpaced) {
    static constexpr auto PACED = std::to_array({ PresentMode::Mailbox, PresentMode::Immediate });
    static constexpr auto UNPACED = std::to_array({ PresentMode::Immediate, PresentMode::Mailbox });
    return unpaced ? std::span<const PresentMode>{ UNPACED } : std::span<const PresentMode>{ PACED };
}

std::expected<Renderer, core::Error> Renderer::Create(
    platform::Window& window,
    const std::filesystem::path& assets,
    std::shared_ptr<const core::Manifest> manifest,
    std::shared_ptr<const core::Strings> strings,
    const TextOptions& text,
    const core::UiColours& colours,
    bool unpaced
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

    // Rendering is uncapped, in PresentModeOrder(); vsync is always there.
    SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
    for (const PresentMode uncapped : PresentModeOrder(unpaced)) {
        SDL_GPUPresentMode candidate = SDL_GPU_PRESENTMODE_VSYNC;
        if (uncapped == PresentMode::Immediate) {
            candidate = SDL_GPU_PRESENTMODE_IMMEDIATE;
        }
        else if (uncapped == PresentMode::Mailbox) {
            candidate = SDL_GPU_PRESENTMODE_MAILBOX;
        }
        if (SDL_WindowSupportsGPUPresentMode(device, impl->window, candidate)) {
            mode = candidate;
            break;
        }
    }
    // Always plain SDR, which SDL guarantees: the composite encodes sRGB itself.
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

    // Pipelines are made at start-up, so each backend checks the shaders when the game starts.
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
    if (std::expected<void, core::Error> ground = impl->CreateGround(device); !ground) {
        return std::unexpected{ std::move(ground.error()) };
    }
    std::expected<internal::SpriteRenderer, core::Error> terrain =
        internal::SpriteRenderer::Create(device, internal::SCENE_FORMAT, MAX_TERRAIN_INSTANCES, "terrain instances");
    if (!terrain) {
        return std::unexpected{ std::move(terrain.error()) };
    }
    impl->terrain = std::move(*terrain);
    std::expected<internal::SpriteRenderer, core::Error> route =
        internal::SpriteRenderer::Create(device, internal::SCENE_FORMAT, MAX_ROUTE_INSTANCES, "route instances");
    if (!route) {
        return std::unexpected{ std::move(route.error()) };
    }
    impl->route = std::move(*route);
    std::expected<internal::SpriteRenderer, core::Error> clouds =
        internal::SpriteRenderer::Create(device, internal::SCENE_FORMAT, MAX_CLOUD_INSTANCES, "cloud instances");
    if (!clouds) {
        return std::unexpected{ std::move(clouds.error()) };
    }
    impl->clouds = std::move(*clouds);
    std::expected<internal::SpriteRenderer, core::Error> cursor =
        internal::SpriteRenderer::Create(device, internal::SCENE_FORMAT, MAX_TARGET_INSTANCES, "target instances");
    if (!cursor) {
        return std::unexpected{ std::move(cursor.error()) };
    }
    impl->cursor = std::move(*cursor);
    std::expected<internal::GlowTableBuffer, core::Error> glowTable =
        internal::GlowTableBuffer::Create(device, BuildGlowTable(impl->effects, impl->uiColours));
    if (!glowTable) {
        return std::unexpected{ std::move(glowTable.error()) };
    }
    impl->glowTable = std::move(*glowTable);
    std::expected<internal::FogTexture, core::Error> fog = internal::FogTexture::Create(device);
    if (!fog) {
        return std::unexpected{ std::move(fog.error()) };
    }
    impl->fog = std::move(*fog);
    std::expected<internal::RibbonRenderer, core::Error> ribbons =
        internal::RibbonRenderer::Create(device, internal::SCENE_FORMAT);
    if (!ribbons) {
        return std::unexpected{ std::move(ribbons.error()) };
    }
    impl->ribbons = std::move(*ribbons);
    std::expected<internal::SealRenderer, core::Error> seals = internal::SealRenderer::Create(device, internal::SCENE_FORMAT);
    if (!seals) {
        return std::unexpected{ std::move(seals.error()) };
    }
    impl->seals = std::move(*seals);
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
        const std::expected<internal::DecodedSprite, core::Error>& image = load.decoded.at(load.uploaded);
        if (image) {
            load.transformNs += image->fieldNs;
        }
        const std::expected<void, core::Error> uploaded = image.and_then([&](const internal::DecodedSprite& decoded) {
            return internal::UploadSprite(
                impl.device.get(),
                impl.sprites,
                impl.manifest->Sprite(load.sprites.at(load.uploaded)),
                impl.manifest->Edge(),
                decoded
            );
        });
        if (!uploaded) {
            core::Error error = uploaded.error();
            impl.load.reset();
            return std::unexpected{ std::move(error) };
        }
        load.decoded.at(load.uploaded) = internal::DecodedSprite{}; // its pixels are on the GPU now
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
    PN_LOG_DEBUG(
        "{} sprites of scene {} decoded and uploaded; distance fields: {} bytes, {:.1f} ms to compute (all jobs)",
        load.sprites.size(),
        impl.manifest->Scene(load.scene).name,
        internal::FieldBytes(*impl.manifest),
        static_cast<f64>(load.transformNs) / static_cast<f64>(core::NS_PER_MS)
    );
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
    m_impl->GlowsChanged();
}

void Renderer::SetEffects(const core::Effects& effects) {
    m_impl->effects = effects;
    m_impl->GlowsChanged();
}

void Renderer::SetSeals(const core::Seals& seals) {
    m_impl->sealLooks = seals;
    m_impl->seals.SetPack(PackSeals(seals, *m_impl->manifest), seals);
}

void Renderer::SetNames(std::shared_ptr<const core::Names> names) {
    if (names == m_impl->names) {
        return; // the reload kept the table: its glyphs are there
    }
    m_impl->nameTexts.Clear();
    m_impl->names = std::move(names);
    if (m_impl->names) {
        m_impl->text->PrewarmNames(*m_impl->strings, *m_impl->names);
    }
}

void Renderer::SetWorld(std::shared_ptr<const core::WorldMap> map, const core::WorldLook& look) {
    m_impl->worldMap = std::move(map);
    m_impl->worldLook = look;
    ++m_impl->worldVersion;
}

f32 Renderer::HudLineWidth(std::string_view line) {
    return m_impl->HudWidth(m_impl->text->Lay(line, core::TextDirection::LeftToRight));
}

void Renderer::SetTargetTile(std::optional<core::Vec2> centre) {
    m_impl->targetTile = centre;
}

void Renderer::SetReduceFlashing(bool reduce) {
    m_impl->reduceFlashing = reduce;
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
    // A picture has every label: none waits for a later frame's budget.
    m_impl->text->SetLayoutBudget(std::numeric_limits<u64>::max());
    std::expected<void, core::Error> uploaded = m_impl->Upload(commands, shown, jobs);
    m_impl->text->SetLayoutBudget(LAYOUT_BUDGET_NS);
    return AfterUpload(
        std::move(uploaded),
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
