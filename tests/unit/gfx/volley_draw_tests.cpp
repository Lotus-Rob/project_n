#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/frame.hpp"
#include "core/instances.hpp"
#include "core/jobs.hpp"
#include "core/manifest.hpp"
#include "core/tick_clock.hpp"
#include "core/ui_colours.hpp"
#include "core/volley.hpp"
#include "gfx/mesh_vertex.hpp"
#include "gfx/volley_draw.hpp"

#include <doctest/doctest.h>

namespace {

// The shipped combat rules (assets/data/abilities.yaml's `combat:` section, which a data test checks).
constexpr core::CombatRules RULES{};

using core::atlas::SpriteId;

constexpr u64 START = 600;
constexpr u64 SEED = 0x5eed'0123'4567'89abULL;
constexpr core::Vec2 ORIGIN{ .x = 100.0F, .y = 50.0F };
constexpr core::Vec2 POINT{ .x = 700.0F, .y = -100.0F };
constexpr core::StableId FOX{ 9 };
// The shipped swords (assets/data/abilities.yaml) in ticks, as their cast resolves them.
constexpr core::VolleyShape SWORDS{
    .count = 1'000,
    .waves = 10,
    .ringRadius = 160.0F,
    .ringStep = 38.0F,
    .scatter = 0.0F,
    .angleJitter = 0.0F,
    .staggerTicks = 3,
    .waveGapTicks = 9,
    .outTicks = 7,
    .holdTicks = 18,
    .departJitterTicks = 15,
    .flightTicks = 102,
    .sweep = 420.0F,
    .sweepJitter = 0.6F,
    .fadeTicks = 18,
    .commitTicks = core::CommitTicksFor(260.0F, core::VolleyShape{}),
};
// The shipped looks (assets/data/effects.yaml, which a data test checks).
constexpr core::Effects EFFECTS{};
constexpr core::Volley VOLLEY{ .use = 7, .startTick = START, .seed = SEED, .origin = ORIGIN, .point = POINT, .shape = SWORDS };
// A fire bolt: one straight flight from the caster.
constexpr core::Volley BOLT{ .use = 8,
                             .startTick = START,
                             .seed = SEED,
                             .origin = ORIGIN,
                             .point = POINT,
                             .shape = { .count = 1, .flightTicks = 60, .commitTicks = core::MIN_COMMIT_TICKS } };
constexpr std::array<u32, 4> SAMPLED_SWORDS{ { 0, 1, 499, 999 } };
// Every sword has appeared (the last wave starts last and its last sword appears last), and none has arrived: the
// inner rings are in flight while the outer ones still stand.
constexpr u64 ALL_APPEARED =
    START + (u64{ SWORDS.waves - 1 } * SWORDS.waveGapTicks) + (SWORDS.staggerTicks - 1) + SWORDS.outTicks;
static_assert(ALL_APPEARED < START + SWORDS.outTicks + SWORDS.holdTicks + SWORDS.flightTicks);
constexpr f32 CLOSE = 1e-4F;  // relative
constexpr f32 HALFWAY = 0.5F; // of a tick, as the frame's alpha
constexpr f32 HALF = 0.5F;

// A manifest whose sprites are 16 by 16 with their foot at their centre, but for the sizes the drawing reads: the sword's,
// the halo's and the bars' square, whose foot is at its left middle. Each at its own place on the page.
core::Manifest TestManifest() {
    constexpr u32 PAGE_SIZE = 1'024;
    constexpr u32 MIP_LEVELS = 5;
    constexpr u16 SIDE = 16;
    constexpr u16 STRIDE = 50;
    std::vector<core::SpriteInfo> sprites;
    for (std::size_t sprite = 0; sprite < core::atlas::SPRITE_NAMES.size(); ++sprite) {
        const auto place = static_cast<u16>(sprite * STRIDE);
        sprites.push_back(
            { .file = "sprites/test.png",
              .x = place,
              .y = place,
              .width = SIDE,
              .height = SIDE,
              .footX = SIDE / 2,
              .footY = SIDE / 2 }
        );
    }
    struct Sized {
        SpriteId sprite = SpriteId{};
        u16 width = 0;
        u16 height = 0;
        u16 footX = 0;
        u16 footY = 0;
    };
    constexpr std::array<Sized, 3> SIZES{ {
        { .sprite = SpriteId::Sword, .width = 10, .height = 40, .footX = 5, .footY = 20 },
        { .sprite = SpriteId::Halo, .width = 48, .height = 48, .footX = 24, .footY = 24 },
        { .sprite = SpriteId::Solid, .width = 8, .height = 8, .footX = 0, .footY = 4 },
    } };
    for (const Sized& sized : SIZES) {
        core::SpriteInfo& sprite = sprites.at(std::to_underlying(sized.sprite));
        sprite.width = sized.width;
        sprite.height = sized.height;
        sprite.footX = sized.footX;
        sprite.footY = sized.footY;
    }
    return { .pageSize = PAGE_SIZE, .pages = 1, .mipLevels = MIP_LEVELS, .sprites = std::move(sprites), .scenes = {} };
}

core::VolleyView SwordsView(core::HitKind element = core::HitKind::Damage, u32 firstTarget = 0) {
    return { .volley = VOLLEY,
             .ability = core::AbilityKind::FlyingSwords,
             .element = element,
             .firstTarget = firstTarget,
             .targets = 1 };
}

core::VolleyView BoltView(u32 firstTarget) {
    return { .volley = BOLT,
             .ability = core::AbilityKind::FireBolt,
             .element = core::HitKind::Burn,
             .firstTarget = firstTarget,
             .targets = 1 };
}

gfx::VolleyDrawing Drawn(
    std::span<const core::VolleyView> volleys,
    std::span<const core::VolleyTarget> targets,
    u64 tick,
    f32 alpha,
    const core::UiColours& colours = {},
    u32 workers = 1,
    const core::Effects& effects = EFFECTS
) {
    core::Jobs jobs{ workers };
    gfx::VolleyDrawing drawing;
    gfx::DrawVolleys(volleys, targets, tick, alpha, TestManifest(), colours, effects, jobs, drawing);
    return drawing;
}

// The `index`th sword's instance in `batch`, across the volleys.
const core::Instance& InBatch(const gfx::VolleyDrawing& drawing, std::size_t batch, u32 index) {
    REQUIRE(batch < drawing.batches.size());
    const std::size_t instance = drawing.batches.at(batch).first + index;
    REQUIRE(instance < drawing.instances.size());
    return drawing.instances.at(instance);
}

// The core, the halo and the sword instances of the `index`th sword: their batches come first, second and last.
const core::Instance& CoreOf(const gfx::VolleyDrawing& drawing, u32 index) {
    return InBatch(drawing, 0, index);
}
const core::Instance& HaloOf(const gfx::VolleyDrawing& drawing, u32 index) {
    return InBatch(drawing, 1, index);
}
const core::Instance& SwordOf(const gfx::VolleyDrawing& drawing, u32 index) {
    REQUIRE(!drawing.batches.empty());
    return InBatch(drawing, drawing.batches.size() - 1, index);
}

// Every bar of `bars` shown whole, as a label that reads alone would show it.
std::vector<gfx::BarShown> Whole(std::span<const core::HealthBar> bars) {
    std::vector<gfx::BarShown> shown;
    for (const core::HealthBar& bar : bars) {
        shown.push_back({ .entity = bar.entity, .opacity = 1.0F });
    }
    std::ranges::sort(shown, {}, [](const gfx::BarShown& one) { return std::to_underlying(one.entity); });
    return shown;
}

constexpr u32 ALPHA_SHIFT = 24;
constexpr u32 CHANNEL_BITS = 8;
constexpr u32 CHANNEL_MASK = 0xFFU;
constexpr u32 CHANNELS = 4;

// `colour` with each premultiplied channel times `factor`, rounded to nearest.
u32 Times(u32 colour, f32 factor) {
    u32 scaled = 0;
    for (u32 channel = 0; channel < CHANNELS; ++channel) {
        const auto value = static_cast<f32>((colour >> (channel * CHANNEL_BITS)) & CHANNEL_MASK);
        scaled |= static_cast<u32>(std::lround(value * factor)) << (channel * CHANNEL_BITS);
    }
    return scaled;
}

std::span<const std::byte> BytesOf(std::span<const core::Instance> instances) {
    return std::as_bytes(instances);
}

// The `corner`th vertex of the `quad`th quad of the `index`th sword's trail.
const gfx::RibbonVertex& TrailVertex(const gfx::VolleyDrawing& drawing, u32 index, u32 quad, u32 corner) {
    const std::size_t vertex = (((std::size_t{ index } * gfx::TRAIL_SEGMENTS) + quad) * gfx::QUAD_VERTICES) + corner;
    REQUIRE(vertex < drawing.ribbons.size());
    return drawing.ribbons.at(vertex);
}

// Where `sword` of VOLLEY aims on a target whose middle is at `middle` (core::AimOffset()).
core::Vec2 Spot(core::Vec2 middle, u32 sword) {
    return middle + core::AimOffset(VOLLEY, sword);
}

core::Vec2 Lerp(core::Vec2 from, core::Vec2 towards, f32 alpha) {
    return from + ((towards - from) * alpha);
}

core::Vec2 Middle(const gfx::RibbonVertex& one, const gfx::RibbonVertex& other) {
    return { .x = (one.x + other.x) * HALF, .y = (one.y + other.y) * HALF };
}

bool IsZero(const core::Instance& instance) {
    const core::Instance zero{};
    return std::ranges::equal(std::as_bytes(std::span{ &instance, 1 }), std::as_bytes(std::span{ &zero, 1 }));
}

void CheckAt(core::Vec2 drawn, core::Vec2 wanted) {
    CHECK(drawn.x == doctest::Approx(wanted.x).epsilon(CLOSE));
    CHECK(drawn.y == doctest::Approx(wanted.y).epsilon(CLOSE));
}

TEST_CASE("each sword is drawn where the shared path puts it at the render time") {
    constexpr core::Vec2 BEFORE{ .x = 650.0F, .y = -80.0F };
    constexpr core::Vec2 AFTER{ .x = 700.0F, .y = -100.0F };
    const std::array<core::VolleyTarget, 1> targets{ { { .entity = FOX, .previous = BEFORE, .current = AFTER } } };
    const std::array<core::VolleyView, 1> volleys{ { SwordsView() } };
    for (const f32 alpha : { 0.3F, 0.7F }) {
        CAPTURE(alpha);
        const gfx::VolleyDrawing drawing = Drawn(volleys, targets, ALL_APPEARED, alpha);
        const f64 renderTick = core::RenderTick(ALL_APPEARED, static_cast<f64>(alpha));
        for (const u32 sword : SAMPLED_SWORDS) {
            CAPTURE(sword);
            const core::SwordPose pose = core::SwordAt(VOLLEY, sword, renderTick, Spot(Lerp(BEFORE, AFTER, alpha), sword));
            const core::Instance& drawn = SwordOf(drawing, sword);
            CheckAt(drawn.current, pose.position);
            CheckAt(drawn.previous, pose.position);
            CheckAt(HaloOf(drawing, sword).current, pose.position);
        }
    }
}

TEST_CASE("a sword not yet launched shows nothing, nor any trail") {
    const std::array<core::VolleyTarget, 1> targets{ { { .entity = FOX, .previous = POINT, .current = POINT } } };
    const std::array<core::VolleyView, 1> volleys{ { SwordsView() } };
    constexpr u64 TICK = START + 2;
    const gfx::VolleyDrawing drawing = Drawn(volleys, targets, TICK, HALFWAY);
    // The second wave's first sword (the next wave to start, nine ticks in), one from the middle and the volley's last:
    // all launch after the tick drawn, and the last launches last.
    u32 secondWave = 0; // the first sword of wave 1: the waves are runs of indices in order
    while (core::WaveOf(VOLLEY, secondWave) < 1) {
        ++secondWave;
    }
    for (const u32 sword : { secondWave, 499U, 999U }) {
        REQUIRE(static_cast<f64>(core::LaunchTick(VOLLEY, sword)) > core::RenderTick(TICK, static_cast<f64>(HALFWAY)));
        CAPTURE(sword);
        CHECK(IsZero(SwordOf(drawing, sword)));
        CHECK(IsZero(HaloOf(drawing, sword)));
        for (u32 quad = 0; quad < gfx::TRAIL_SEGMENTS; ++quad) {
            for (u32 corner = 0; corner < gfx::QUAD_VERTICES; ++corner) {
                CHECK(TrailVertex(drawing, sword, quad, corner).colour == 0);
            }
        }
    }
}

TEST_CASE("a sword still on its ring shows, but draws no trail") {
    const std::array<core::VolleyTarget, 1> targets{ { { .entity = FOX, .previous = POINT, .current = POINT } } };
    const std::array<core::VolleyView, 1> volleys{ { SwordsView() } };
    // The first wave has appeared and holds still on its ring.
    constexpr u64 TICK = START + SWORDS.outTicks + 2;
    const gfx::VolleyDrawing drawing = Drawn(volleys, targets, TICK, HALFWAY);
    // Three of the first wave's swords: its first, one in its middle and one near its end.
    constexpr std::array<u32, 3> HOLDING{ { 0, 24, 47 } };
    bool shown = true;
    bool trailed = false;
    for (const u32 sword : HOLDING) {
        REQUIRE(core::WaveOf(VOLLEY, sword) == 0);
        REQUIRE(core::DepartureTick(VOLLEY, sword) > TICK + 1); // still on its ring
        shown = shown && !IsZero(SwordOf(drawing, sword));
        for (u32 quad = 0; quad < gfx::TRAIL_SEGMENTS; ++quad) {
            for (u32 corner = 0; corner < gfx::QUAD_VERTICES; ++corner) {
                trailed = trailed || TrailVertex(drawing, sword, quad, corner).colour != 0;
            }
        }
    }
    CHECK(shown);
    CHECK_FALSE(trailed);
}

TEST_CASE("the trail is sampled from the same path a little back in time") {
    const std::array<core::VolleyTarget, 1> targets{ { { .entity = FOX, .previous = POINT, .current = POINT } } };
    const std::array<core::VolleyView, 1> volleys{ { SwordsView() } };
    constexpr f32 ALPHA = HALFWAY;
    constexpr u32 LAST = gfx::TRAIL_SEGMENTS - 1;
    for (const u32 sword : SAMPLED_SWORDS) {
        CAPTURE(sword);
        // Each a whole trail into its flight, which lasts far longer.
        const u64 tick = core::DepartureTick(VOLLEY, sword) + static_cast<u64>(EFFECTS.flyingSwords.trailTicks) + 1;
        REQUIRE(tick < core::ArrivalTick(VOLLEY, sword));
        const gfx::VolleyDrawing drawing = Drawn(volleys, targets, tick, ALPHA);
        const f64 renderTick = core::RenderTick(tick, static_cast<f64>(ALPHA));
        const core::SwordPose head = core::SwordAt(VOLLEY, sword, renderTick, Spot(POINT, sword));
        const core::SwordPose tail =
            core::SwordAt(VOLLEY, sword, renderTick - static_cast<f64>(EFFECTS.flyingSwords.trailTicks), Spot(POINT, sword));
        // The first quad's first pair is at the sword, the last quad's last pair at the tail, one on either side.
        CheckAt(Middle(TrailVertex(drawing, sword, 0, 0), TrailVertex(drawing, sword, 0, 1)), head.position);
        const gfx::RibbonVertex& left = TrailVertex(drawing, sword, LAST, 2);
        const gfx::RibbonVertex& right = TrailVertex(drawing, sword, LAST, 3);
        CheckAt(Middle(left, right), tail.position);
        CHECK(std::hypot(left.x - right.x, left.y - right.y) > 1.0F);
        CHECK((left.uv & 0xFFFFU) == 0xFFFFU);                        // u: at the tail
        CHECK((TrailVertex(drawing, sword, 0, 0).uv & 0xFFFFU) == 0); // at the sword
    }
}

TEST_CASE("the cores draw under the halos, the halos under the bars, and the swords over all") {
    const std::array<core::VolleyTarget, 2> targets{ { { .entity = FOX, .previous = POINT, .current = POINT },
                                                       { .entity = FOX, .previous = POINT, .current = POINT } } };
    const std::array<core::VolleyView, 2> volleys{ { SwordsView(), BoltView(1) } };
    const std::array<core::HealthBar, 1> bars{ { { .entity = FOX, .previous = POINT, .current = POINT, .fraction = HALF } } };
    constexpr u32 SWORDS_DRAWN = 1'001;
    constexpr u32 BAR_INSTANCES = 2;
    core::Jobs jobs{ 1 };
    gfx::VolleyDrawing drawing;
    gfx::DrawHealthBars(bars, Whole(bars), 1.0F, TestManifest(), {}, EFFECTS, drawing);
    gfx::DrawVolleys(volleys, targets, ALL_APPEARED, 1.0F, TestManifest(), {}, EFFECTS, jobs, drawing);
    REQUIRE(drawing.batches.size() == 4);
    const core::Batch& cores = drawing.batches[0];
    const core::Batch& halos = drawing.batches[1];
    const core::Batch& bar = drawing.batches[2];
    const core::Batch& swords = drawing.batches[3];
    CHECK(cores.pipeline == core::Pipeline::Normal);
    CHECK(cores.first == BAR_INSTANCES);
    CHECK(cores.count == SWORDS_DRAWN);
    CHECK(halos.pipeline == core::Pipeline::Additive);
    CHECK(halos.first == BAR_INSTANCES + SWORDS_DRAWN);
    CHECK(halos.count == SWORDS_DRAWN);
    CHECK(bar.pipeline == core::Pipeline::Normal);
    CHECK(bar.first == 0);
    CHECK(bar.count == BAR_INSTANCES);
    CHECK(swords.pipeline == core::Pipeline::Normal);
    CHECK(swords.first == BAR_INSTANCES + (2 * SWORDS_DRAWN));
    CHECK(swords.count == SWORDS_DRAWN);
    CHECK(drawing.instances.size() == BAR_INSTANCES + (gfx::SWORD_INSTANCES * SWORDS_DRAWN));
    CHECK(drawing.ribbons.size() == std::size_t{ SWORDS_DRAWN } * gfx::TRAIL_SEGMENTS * gfx::QUAD_VERTICES);
    CHECK(drawing.droppedSwords == 0);
}

TEST_CASE("swords past the overlay's or the trails' room are left out whole, never a part without the rest") {
    // Two of the largest volleys: more swords than the overlay holds three instances of, or the ribbons trails of.
    core::VolleyView largest = SwordsView();
    largest.volley.shape.count = core::MAX_VOLLEY_COUNT;
    const std::array<core::VolleyView, 2> volleys{ { largest, largest } };
    const std::array<core::VolleyTarget, 1> targets{ { { .entity = FOX, .previous = POINT, .current = POINT } } };
    const std::array<core::HealthBar, 1> bars{ { { .entity = FOX, .previous = POINT, .current = POINT, .fraction = HALF } } };
    constexpr u32 BAR_INSTANCES = 2;
    // The trails are the tighter room: 16,384 quads hold 2,048 swords' trails, fewer than the overlay's 2,730.
    constexpr u32 FITTING = std::min(
        (gfx::MAX_OVERLAY_INSTANCES - BAR_INSTANCES) / gfx::SWORD_INSTANCES, gfx::MAX_RIBBON_QUADS / gfx::TRAIL_SEGMENTS
    );
    core::Jobs jobs{ 1 };
    gfx::VolleyDrawing drawing;
    gfx::DrawHealthBars(bars, Whole(bars), 1.0F, TestManifest(), {}, EFFECTS, drawing);
    gfx::DrawVolleys(volleys, targets, ALL_APPEARED, 1.0F, TestManifest(), {}, EFFECTS, jobs, drawing);
    CHECK(drawing.instances.size() <= gfx::MAX_OVERLAY_INSTANCES);
    REQUIRE(drawing.batches.size() == 4);
    CHECK(drawing.batches[0].count == FITTING);
    CHECK(drawing.batches[1].count == FITTING);
    CHECK(drawing.batches[3].count == FITTING);
    CHECK(drawing.droppedSwords == (2 * core::MAX_VOLLEY_COUNT) - FITTING);
    CHECK(drawing.ribbons.size() == std::size_t{ FITTING } * gfx::TRAIL_SEGMENTS * gfx::QUAD_VERTICES);
    CHECK(drawing.ribbons.size() <= std::size_t{ gfx::MAX_RIBBON_QUADS } * gfx::QUAD_VERTICES);
    drawing.Clear();
    CHECK(drawing.droppedSwords == 0);
    // The one room rule, which the renderer's overflow report names too.
    CHECK(gfx::SwordRoom(BAR_INSTANCES, 0) == FITTING);
    CHECK(gfx::MAX_DRAWN_SWORDS == gfx::MAX_RIBBON_QUADS / gfx::TRAIL_SEGMENTS);
}

TEST_CASE("the HUD's bars are placed first and keep their room: the largest volleys leave them whole") {
    constexpr core::Vec2 TOP_LEFT{ .x = 16.0F, .y = 600.0F };
    constexpr core::Vec2 SIZE{ .x = 240.0F, .y = 18.0F };
    constexpr u32 FILL = 0xFF00'00FFU;
    constexpr u32 BACKGROUND = 0xFF40'2010U;
    constexpr u32 HUD_INSTANCES = 4; // two bars, each a background and a fill
    const std::array<gfx::HudBar, 2> bars{ {
        { .topLeft = TOP_LEFT, .size = SIZE, .fraction = HALF, .fill = FILL, .background = BACKGROUND },
        { .topLeft = TOP_LEFT + SIZE, .size = SIZE, .fraction = 1.0F, .fill = FILL, .background = BACKGROUND },
    } };
    core::VolleyView largest = SwordsView();
    largest.volley.shape.count = core::MAX_VOLLEY_COUNT;
    const std::array<core::VolleyView, 2> volleys{ { largest, largest } };
    const std::array<core::VolleyTarget, 1> targets{ { { .entity = FOX, .previous = POINT, .current = POINT } } };
    const core::Manifest manifest = TestManifest();
    core::Jobs jobs{ 1 };
    gfx::VolleyDrawing drawing;
    gfx::DrawHudBars(bars, manifest, drawing);
    REQUIRE(drawing.hud.size() == 1);
    CHECK(drawing.hud[0].first == 0);
    CHECK(drawing.hud[0].count == HUD_INSTANCES);
    CHECK(drawing.hud[0].pipeline == core::Pipeline::Normal);
    const std::vector<core::Instance> placed = drawing.instances;
    REQUIRE(placed.size() == HUD_INSTANCES);
    // The background over the whole bar, the fill from its left over the fraction, in their colours.
    CHECK(placed[0].size == core::PackPair(core::PackHalf(SIZE.x), core::PackHalf(SIZE.y)));
    CHECK(placed[1].size == core::PackPair(core::PackHalf(SIZE.x * HALF), core::PackHalf(SIZE.y)));
    CheckAt(placed[0].current, TOP_LEFT + (SIZE * HALF));
    CheckAt(placed[1].current, { .x = TOP_LEFT.x + (SIZE.x * HALF * HALF), .y = TOP_LEFT.y + (SIZE.y * HALF) });
    CHECK(placed[0].colour == BACKGROUND);
    CHECK(placed[1].colour == FILL);

    gfx::DrawVolleys(volleys, targets, ALL_APPEARED, 1.0F, manifest, {}, EFFECTS, jobs, drawing);
    CHECK(drawing.instances.size() <= gfx::MAX_OVERLAY_INSTANCES);
    CHECK(drawing.batches.back().count == gfx::SwordRoom(HUD_INSTANCES, 0));
    const bool whole = std::ranges::equal(BytesOf(placed), BytesOf(std::span{ drawing.instances }.first(HUD_INSTANCES)));
    CHECK(whole);
    REQUIRE(drawing.hud.size() == 1); // the swords' batches are the world's
    drawing.Clear();
    CHECK(drawing.hud.empty());
}

TEST_CASE("the lance's burst grows and fades in its element's colour, from its start to its end") {
    constexpr u64 SIX_TICKS = 6;
    constexpr f32 SIDE = 16.0F; // the test manifest's burst
    const std::array<core::Flash, 1> flashes{
        { { .point = POINT, .startTick = START, .ticks = RULES.flashTicks, .element = core::HitKind::Poison } }
    };
    const core::Manifest manifest = TestManifest();
    const auto drawn = [&](f64 renderTick, const core::UiColours& colours = {}) {
        gfx::VolleyDrawing drawing;
        gfx::DrawFlashes(flashes, renderTick, manifest, colours, EFFECTS, drawing);
        return drawing;
    };
    const auto sized = [](f32 scale) { return core::PackPair(core::PackHalf(SIDE * scale), core::PackHalf(SIDE * scale)); };
    const u32 poison = core::UiColours{}.Of(core::UiColour::PoisonEffect);

    constexpr f64 HALF_A_TICK = 0.5;
    const gfx::VolleyDrawing before = drawn(static_cast<f64>(START) - HALF_A_TICK);
    CHECK(before.instances.empty());
    CHECK(before.batches.empty());

    const gfx::VolleyDrawing start = drawn(static_cast<f64>(START));
    REQUIRE(start.instances.size() == 1);
    REQUIRE(start.batches.size() == 1);
    CHECK(start.batches[0].pipeline == core::Pipeline::Additive);
    CheckAt(start.instances[0].current, POINT);
    CHECK(start.instances[0].size == sized(EFFECTS.poisonLance.startScale));
    CHECK(start.instances[0].colour == Times(poison, EFFECTS.poisonLance.intensity));
    CHECK(start.instances[0].regionMin == core::MakeInstance(manifest, { .sprite = SpriteId::LanceStrike }).regionMin);

    // Halfway: half as bright, three quarters of the way to its full size.
    const gfx::VolleyDrawing half = drawn(static_cast<f64>(START + SIX_TICKS));
    REQUIRE(half.instances.size() == 1);
    constexpr f32 THREE_QUARTERS = 0.75F;
    CHECK(
        half.instances[0].size ==
        sized(
            EFFECTS.poisonLance.startScale + ((EFFECTS.poisonLance.endScale - EFFECTS.poisonLance.startScale) * THREE_QUARTERS)
        )
    );
    CHECK(half.instances[0].colour == Times(poison, EFFECTS.poisonLance.intensity * HALF));

    const gfx::VolleyDrawing end = drawn(static_cast<f64>(START + RULES.flashTicks));
    CHECK(end.instances.empty());

    constexpr u32 GREEN = 0xFF00'FF00U;
    core::UiColours recoloured;
    recoloured.Set(core::UiColour::PoisonEffect, GREEN);
    CHECK(drawn(static_cast<f64>(START), recoloured).instances.at(0).colour == GREEN);
}

TEST_CASE("the same input gives the same bytes with one worker and with many") {
    constexpr u32 MANY = 7;
    // The first fox dies mid-flight, so swords swing to the second; the bolt flies at the second.
    constexpr u64 DIED = START + 40;
    const std::array<core::VolleyTarget, 3> targets{ {
        { .entity = FOX, .previous = { .x = 640.0F, .y = -90.0F }, .current = { .x = 640.0F, .y = -90.0F }, .diedTick = DIED },
        { .entity = core::StableId{ 10 }, .previous = { .x = 760.0F, .y = -60.0F }, .current = { .x = 765.0F, .y = -58.0F } },
        { .entity = core::StableId{ 10 }, .previous = { .x = 760.0F, .y = -60.0F }, .current = { .x = 765.0F, .y = -58.0F } },
    } };
    core::VolleyView swords = SwordsView();
    swords.targets = 2;
    const std::array<core::VolleyView, 2> volleys{ { swords, BoltView(2) } };
    for (const u64 tick : { START + 3, ALL_APPEARED, START + 85, START + 100 }) {
        CAPTURE(tick);
        const gfx::VolleyDrawing one = Drawn(volleys, targets, tick, 0.4F, {}, 1);
        const gfx::VolleyDrawing many = Drawn(volleys, targets, tick, 0.4F, {}, MANY);
        CHECK(std::ranges::equal(std::as_bytes(std::span{ one.instances }), std::as_bytes(std::span{ many.instances })));
        CHECK(std::ranges::equal(std::as_bytes(std::span{ one.ribbons }), std::as_bytes(std::span{ many.ribbons })));
    }
}

TEST_CASE("a volley's swords follow a moving target between ticks") {
    constexpr core::Vec2 BEFORE{ .x = 600.0F, .y = 0.0F };
    constexpr core::Vec2 AFTER{ .x = 640.0F, .y = 20.0F };
    const std::array<core::VolleyTarget, 1> targets{ { { .entity = FOX, .previous = BEFORE, .current = AFTER } } };
    const std::array<core::VolleyView, 1> volleys{ { SwordsView() } };
    // The swords arriving on the tick drawn: in their last tick of flight they close on the target as it moves.
    std::vector<u32> arriving;
    const u64 tick = core::ArrivalTick(VOLLEY, 0);
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        if (core::ArrivalTick(VOLLEY, sword) == tick) {
            arriving.push_back(sword);
        }
    }
    REQUIRE(!arriving.empty());
    // The first, the middle and the last of them.
    arriving = { arriving.front(), arriving.at(arriving.size() / 2), arriving.back() };
    constexpr std::array<f32, 4> ALPHAS{ { 0.25F, 0.5F, 0.75F, 1.0F } };
    for (std::size_t index = 0; index < ALPHAS.size(); ++index) {
        const f32 alpha = ALPHAS.at(index);
        CAPTURE(alpha);
        const gfx::VolleyDrawing drawing = Drawn(volleys, targets, tick, alpha);
        for (const u32 sword : arriving) {
            CAPTURE(sword);
            const core::Vec2 aim = Spot(Lerp(BEFORE, AFTER, alpha), sword);
            const core::Vec2 drawn = SwordOf(drawing, sword).current;
            CheckAt(drawn, core::SwordAt(VOLLEY, sword, core::RenderTick(tick, static_cast<f64>(alpha)), aim).position);
            if (index + 1 == ALPHAS.size()) {
                CheckAt(drawn, Spot(AFTER, sword)); // at its arrival, on the target where this tick left it
            }
        }
    }
}

TEST_CASE("a sword that struck its target is gone after its arrival; one that plunged stays at its aim and fades") {
    constexpr core::Vec2 DEAD_AT{ .x = 620.0F, .y = -70.0F };
    constexpr core::StableId OTHER{ 10 };
    // Swords fly at the first; the second's (all its own) died at the cast: nothing living to move on to, they plunge.
    const std::array<core::VolleyTarget, 2> targets{ {
        { .entity = FOX, .previous = POINT, .current = POINT },
        { .entity = OTHER, .previous = DEAD_AT, .current = DEAD_AT, .diedTick = START },
    } };
    core::VolleyView striking = SwordsView();
    core::VolleyView plunging = SwordsView(core::HitKind::Damage, 1);
    const std::array<core::VolleyView, 2> volleys{ { striking, plunging } };
    constexpr u32 SWORD = 0;
    constexpr u64 AFTER_TICKS = 6;
    const u64 tick = core::ArrivalTick(VOLLEY, SWORD) + AFTER_TICKS;
    const gfx::VolleyDrawing drawing = Drawn(volleys, targets, tick, HALFWAY);
    CHECK(IsZero(SwordOf(drawing, SWORD)));
    CHECK(IsZero(HaloOf(drawing, SWORD)));
    const core::Instance& plunged = SwordOf(drawing, SWORDS.count + SWORD);
    CheckAt(plunged.current, Spot(DEAD_AT, SWORD));
    const f32 shown =
        core::SwordAt(VOLLEY, SWORD, core::RenderTick(tick, static_cast<f64>(HALFWAY)), Spot(DEAD_AT, SWORD)).shown;
    REQUIRE(shown > 0.0F);
    REQUIRE(shown < 1.0F);
    CHECK((plunged.colour >> ALPHA_SHIFT) < 0xFFU);
    CHECK((plunged.colour >> ALPHA_SHIFT) > 0U);
    // Its glow goes before its blade.
    CHECK((HaloOf(drawing, SWORDS.count + SWORD).colour >> ALPHA_SHIFT) < (plunged.colour >> ALPHA_SHIFT));
}

TEST_CASE("a sword whose target dies too near its arrival to move on plunges at its spot where the target died") {
    constexpr core::Vec2 DEAD_AT{ .x = 640.0F, .y = -90.0F };
    constexpr u32 SWORD = 0;
    const u64 arrival = core::ArrivalTick(VOLLEY, SWORD);
    const u64 died = arrival - (SWORDS.commitTicks / 2); // inside the commit window: it keeps its target
    // Two foxes, sword 0's the first (its index modulo two); the second lives on.
    const std::array<core::VolleyTarget, 2> targets{ {
        { .entity = FOX, .previous = DEAD_AT, .current = DEAD_AT, .diedTick = died },
        { .entity = core::StableId{ 10 }, .previous = POINT, .current = POINT },
    } };
    core::VolleyView volley = SwordsView();
    volley.targets = 2;
    const std::array<core::VolleyView, 1> volleys{ { volley } };
    constexpr u64 AFTER_TICKS = 4;
    const gfx::VolleyDrawing drawing = Drawn(volleys, targets, arrival + AFTER_TICKS, HALFWAY);
    const core::Instance& plunged = SwordOf(drawing, SWORD);
    REQUIRE_FALSE(IsZero(plunged));
    CheckAt(plunged.current, Spot(DEAD_AT, SWORD));
    CHECK((plunged.colour >> ALPHA_SHIFT) < 0xFFU);
}

TEST_CASE("a health bar's fill is as long as its health and takes the table's colours") {
    constexpr core::Vec2 BEFORE{ .x = 100.0F, .y = 200.0F };
    constexpr core::Vec2 AFTER{ .x = 120.0F, .y = 220.0F };
    constexpr f32 FRACTION = 0.25F;
    constexpr core::Vec2 MIDDLE{ .x = 110.0F, .y = 210.0F - EFFECTS.healthBar.rise }; // halfway, risen
    const std::array<core::HealthBar, 1> bars{
        { { .entity = FOX, .previous = BEFORE, .current = AFTER, .fraction = FRACTION } }
    };
    const core::Manifest manifest = TestManifest();
    gfx::VolleyDrawing drawing;
    gfx::DrawHealthBars(bars, Whole(bars), HALFWAY, manifest, {}, EFFECTS, drawing);
    REQUIRE(drawing.instances.size() == 2);
    const core::Instance& background = drawing.instances[0];
    const core::Instance& fill = drawing.instances[1];
    CheckAt(background.current, MIDDLE);
    CHECK(background.size == core::PackPair(core::PackHalf(EFFECTS.healthBar.width), core::PackHalf(EFFECTS.healthBar.height)));
    const f32 length = EFFECTS.healthBar.fillWidth * FRACTION;
    CHECK(fill.size == core::PackPair(core::PackHalf(length), core::PackHalf(EFFECTS.healthBar.fillHeight)));
    // From the left of the full fill's place.
    CheckAt(fill.current, { .x = MIDDLE.x - (EFFECTS.healthBar.fillWidth * HALF) + (length * HALF), .y = MIDDLE.y });
    const core::UiColours defaults{};
    CHECK(background.colour == defaults.Of(core::UiColour::BarBackground));
    CHECK(fill.colour == defaults.Of(core::UiColour::EnemyHealthBar));

    constexpr u32 RECOLOURED_FILL = 0xFF00'FF00U;
    constexpr u32 RECOLOURED_BACK = 0xFF40'2010U;
    core::UiColours recoloured;
    recoloured.Set(core::UiColour::EnemyHealthBar, RECOLOURED_FILL);
    recoloured.Set(core::UiColour::BarBackground, RECOLOURED_BACK);
    drawing.Clear();
    gfx::DrawHealthBars(bars, Whole(bars), HALFWAY, manifest, recoloured, EFFECTS, drawing);
    REQUIRE(drawing.instances.size() == 2);
    CHECK(drawing.instances[0].colour == RECOLOURED_BACK);
    CHECK(drawing.instances[1].colour == RECOLOURED_FILL);
}

TEST_CASE("a health bar's fill stops at full and shows nothing for a health that isn't a number") {
    constexpr f32 OVER_FULL = 1.5F;
    const std::array<core::HealthBar, 2> bars{ {
        { .entity = FOX, .previous = POINT, .current = POINT, .fraction = OVER_FULL },
        { .entity = core::StableId{ 10 },
          .previous = POINT,
          .current = POINT,
          .fraction = std::numeric_limits<f32>::quiet_NaN() },
    } };
    gfx::VolleyDrawing drawing;
    gfx::DrawHealthBars(bars, Whole(bars), 1.0F, TestManifest(), {}, EFFECTS, drawing);
    REQUIRE(drawing.instances.size() == 4);
    const u32 full = core::PackPair(core::PackHalf(EFFECTS.healthBar.fillWidth), core::PackHalf(EFFECTS.healthBar.fillHeight));
    const u32 empty = core::PackPair(core::PackHalf(0.0F), core::PackHalf(EFFECTS.healthBar.fillHeight));
    CHECK(drawing.instances[1].size == full);
    CHECK(drawing.instances[3].size == empty);
}

TEST_CASE("a health bar shows as its creature's label does: faded with it, and not at all while it heads a stack") {
    constexpr f32 FADING = 0.4F;
    constexpr f32 CROSSING = 0.25F;
    // Alone and readable; fading out (too small to read, or hidden); heading a stack; leaving a stack, and joining one.
    CHECK(gfx::BarOpacity({ .count = 0, .previous = 0, .change = 1.0F, .opacity = 1.0F }) == doctest::Approx(1.0F));
    CHECK(gfx::BarOpacity({ .count = 0, .previous = 0, .change = 1.0F, .opacity = FADING }) == doctest::Approx(FADING));
    CHECK(gfx::BarOpacity({ .count = 3, .previous = 3, .change = 1.0F, .opacity = 1.0F }) == doctest::Approx(0.0F));
    CHECK(gfx::BarOpacity({ .count = 0, .previous = 2, .change = CROSSING, .opacity = 1.0F }) == doctest::Approx(CROSSING));
    CHECK(
        gfx::BarOpacity({ .count = 2, .previous = 0, .change = CROSSING, .opacity = 1.0F }) == doctest::Approx(1.0F - CROSSING)
    );

    constexpr core::StableId FADED{ 10 };
    constexpr core::StableId UNLABELLED{ 11 };
    const std::array<core::HealthBar, 3> bars{ {
        { .entity = FOX, .previous = POINT, .current = POINT, .fraction = HALF },
        { .entity = FADED, .previous = POINT, .current = POINT, .fraction = HALF },
        { .entity = UNLABELLED, .previous = POINT, .current = POINT, .fraction = HALF },
    } };
    const std::array<gfx::BarShown, 2> shown{ { { .entity = FOX, .opacity = FADING }, { .entity = FADED, .opacity = 0.0F } } };
    gfx::VolleyDrawing drawing;
    gfx::DrawHealthBars(bars, shown, 1.0F, TestManifest(), {}, EFFECTS, drawing);
    REQUIRE(drawing.instances.size() == 2); // only the first's
    const core::UiColours defaults{};
    CHECK(drawing.instances[0].colour == Times(defaults.Of(core::UiColour::BarBackground), FADING));
    CHECK(drawing.instances[1].colour == Times(defaults.Of(core::UiColour::EnemyHealthBar), FADING));
}

TEST_CASE("cores, halos and trails take their element's colour, not their ability's") {
    const std::array<core::VolleyTarget, 2> targets{ { { .entity = FOX, .previous = POINT, .current = POINT },
                                                       { .entity = FOX, .previous = POINT, .current = POINT } } };
    const std::array<core::VolleyView, 2> volleys{ { SwordsView(), BoltView(1) } };
    // The first sword to leave its ring, with its whole trail behind it, while the bolt is still in flight.
    u32 sword = 0;
    for (u32 other = 1; other < SWORDS.count; ++other) {
        sword = core::DepartureTick(VOLLEY, other) < core::DepartureTick(VOLLEY, sword) ? other : sword;
    }
    constexpr u32 THE_BOLT = SWORDS.count;
    const u64 tick = core::DepartureTick(VOLLEY, sword) + static_cast<u64>(EFFECTS.flyingSwords.trailTicks) + 1;
    REQUIRE(tick < core::ArrivalTick(BOLT, 0));
    const core::Manifest manifest = TestManifest();
    const auto check = [&](const core::UiColours& colours) {
        const gfx::VolleyDrawing drawing = Drawn(volleys, targets, tick, 1.0F, colours);
        const u32 damage = colours.Of(core::UiColour::DamageEffect);
        const u32 burn = colours.Of(core::UiColour::BurnEffect);
        CHECK(CoreOf(drawing, sword).colour == Times(damage, EFFECTS.flyingSwords.coreOpacity));
        CHECK(HaloOf(drawing, sword).colour == Times(damage, EFFECTS.flyingSwords.haloIntensity));
        CHECK(TrailVertex(drawing, sword, 0, 0).colour == Times(damage, EFFECTS.flyingSwords.haloIntensity));
        CHECK(SwordOf(drawing, sword).colour == core::OPAQUE_WHITE); // its own steel
        CHECK(CoreOf(drawing, THE_BOLT).colour == Times(burn, EFFECTS.flyingSwords.coreOpacity));
        CHECK(HaloOf(drawing, THE_BOLT).colour == Times(burn, EFFECTS.flyingSwords.haloIntensity));
        CHECK(TrailVertex(drawing, THE_BOLT, 0, 0).colour == Times(burn, EFFECTS.flyingSwords.haloIntensity));
        CHECK(SwordOf(drawing, THE_BOLT).colour == burn); // white art, tinted
        // The ability picks the sword's sprite; every core is the same dot.
        CHECK(SwordOf(drawing, sword).regionMin == core::MakeInstance(manifest, { .sprite = SpriteId::Sword }).regionMin);
        CHECK(SwordOf(drawing, THE_BOLT).regionMin == core::MakeInstance(manifest, { .sprite = SpriteId::FireBolt }).regionMin);
        CHECK(CoreOf(drawing, sword).regionMin == core::MakeInstance(manifest, { .sprite = SpriteId::GlowCore }).regionMin);
        return drawing;
    };
    const gfx::VolleyDrawing before = check({});
    core::UiColours recoloured;
    constexpr u32 GREEN = 0xFF00'FF00U;
    recoloured.Set(core::UiColour::BurnEffect, GREEN);
    const gfx::VolleyDrawing after = check(recoloured);
    // Only the bolt changed: the swords' cores, halos, blades and trails are the same bytes.
    REQUIRE(before.batches.size() == after.batches.size());
    for (std::size_t batch = 0; batch < before.batches.size(); ++batch) {
        CAPTURE(batch);
        const std::span<const core::Instance> swordsBefore =
            std::span{ before.instances }.subspan(before.batches[batch].first, SWORDS.count);
        const std::span<const core::Instance> swordsAfter =
            std::span{ after.instances }.subspan(after.batches[batch].first, SWORDS.count);
        CHECK(std::ranges::equal(BytesOf(swordsBefore), BytesOf(swordsAfter)));
    }
    const std::size_t swordVertices = std::size_t{ SWORDS.count } * gfx::TRAIL_SEGMENTS * gfx::QUAD_VERTICES;
    const std::span<const gfx::RibbonVertex> trailsBefore = std::span{ before.ribbons }.first(swordVertices);
    const std::span<const gfx::RibbonVertex> trailsAfter = std::span{ after.ribbons }.first(swordVertices);
    CHECK(std::ranges::equal(std::as_bytes(trailsBefore), std::as_bytes(trailsAfter)));
    CHECK(CoreOf(after, THE_BOLT).colour == Times(GREEN, EFFECTS.flyingSwords.coreOpacity));
    CHECK(HaloOf(after, THE_BOLT).colour == GREEN);
}

TEST_CASE("each ability draws by its own look in the effects, and new effects show at the next draw") {
    const std::array<core::VolleyTarget, 2> targets{ { { .entity = FOX, .previous = POINT, .current = POINT },
                                                       { .entity = FOX, .previous = POINT, .current = POINT } } };
    const std::array<core::VolleyView, 2> volleys{ { SwordsView(), BoltView(1) } };
    constexpr u32 THE_BOLT = SWORDS.count;
    constexpr u32 SWORD = 0;
    constexpr f32 DOUBLE = 2.0F;
    constexpr f32 DIM = 0.5F;
    const core::Manifest manifest = TestManifest();
    const u64 tick = core::DepartureTick(VOLLEY, SWORD) + static_cast<u64>(EFFECTS.flyingSwords.trailTicks) + 1;
    REQUIRE(tick < core::ArrivalTick(BOLT, 0));
    core::Effects tuned = EFFECTS;
    tuned.flyingSwords.swordScale = EFFECTS.flyingSwords.swordScale * DOUBLE;
    tuned.flyingSwords.haloIntensity = DIM;
    tuned.fireBolt.coreScale = EFFECTS.fireBolt.coreScale * DOUBLE;
    tuned.fireBolt.haloScale = EFFECTS.fireBolt.haloScale * DOUBLE;
    const gfx::VolleyDrawing before = Drawn(volleys, targets, tick, 1.0F);
    const gfx::VolleyDrawing after = Drawn(volleys, targets, tick, 1.0F, {}, 1, tuned);
    const auto sizeOf = [&manifest](SpriteId sprite, f32 scale) {
        return core::MakeInstance(manifest, { .sprite = sprite, .scale = scale }).size;
    };
    CHECK(SwordOf(before, SWORD).size == sizeOf(SpriteId::Sword, EFFECTS.flyingSwords.swordScale));
    CHECK(SwordOf(after, SWORD).size == sizeOf(SpriteId::Sword, tuned.flyingSwords.swordScale));
    CHECK(CoreOf(after, THE_BOLT).size == sizeOf(SpriteId::GlowCore, tuned.fireBolt.coreScale));
    CHECK(HaloOf(after, THE_BOLT).size == sizeOf(SpriteId::Halo, tuned.fireBolt.haloScale));
    // The swords' core was left as it was; only their glow dimmed.
    CHECK(CoreOf(after, SWORD).size == CoreOf(before, SWORD).size);
    const u32 damage = core::UiColours{}.Of(core::UiColour::DamageEffect);
    CHECK(HaloOf(after, SWORD).colour == Times(damage, DIM));
    CHECK(HaloOf(before, SWORD).colour == Times(damage, EFFECTS.flyingSwords.haloIntensity));
}

TEST_CASE("a trail takes its reach, width and taper from the effects") {
    const std::array<core::VolleyTarget, 1> targets{ { { .entity = FOX, .previous = POINT, .current = POINT } } };
    const std::array<core::VolleyView, 1> volleys{ { SwordsView() } };
    constexpr u32 SWORD = 0;
    constexpr f32 LONG = 12.0F;
    constexpr f32 WIDE = 10.0F;
    const u64 tick = core::DepartureTick(VOLLEY, SWORD) + static_cast<u64>(LONG) + 1;
    core::Effects tuned = EFFECTS;
    tuned.flyingSwords.trailTicks = LONG;
    tuned.flyingSwords.trailWidth = WIDE;
    tuned.flyingSwords.trailTaper = 0.0F;
    const gfx::VolleyDrawing before = Drawn(volleys, targets, tick, 1.0F);
    const gfx::VolleyDrawing after = Drawn(volleys, targets, tick, 1.0F, {}, 1, tuned);
    const auto across = [](const gfx::VolleyDrawing& drawing, u32 corner) {
        const gfx::RibbonVertex left = TrailVertex(drawing, SWORD, 0, corner);
        const gfx::RibbonVertex right = TrailVertex(drawing, SWORD, 0, corner + 1);
        return std::hypot(left.x - right.x, left.y - right.y);
    };
    CHECK(across(before, 0) == doctest::Approx(EFFECTS.flyingSwords.trailWidth).epsilon(CLOSE));
    CHECK(across(after, 0) == doctest::Approx(WIDE).epsilon(CLOSE));
    // The tail lies further back along a longer reach.
    const gfx::RibbonVertex headBefore = TrailVertex(before, SWORD, 0, 0);
    const gfx::RibbonVertex tailBefore = TrailVertex(before, SWORD, gfx::TRAIL_SEGMENTS - 1, 2);
    const gfx::RibbonVertex tailAfter = TrailVertex(after, SWORD, gfx::TRAIL_SEGMENTS - 1, 2);
    CHECK(
        std::hypot(tailAfter.x - headBefore.x, tailAfter.y - headBefore.y) >
        std::hypot(tailBefore.x - headBefore.x, tailBefore.y - headBefore.y)
    );
}

TEST_CASE("the health bars and the burst take their sizes from the effects") {
    constexpr f32 DOUBLE = 2.0F;
    constexpr f32 RISE = 20.0F;
    constexpr f64 LAST_MOMENT = 9.999; // of the burst's ten ticks
    const std::array<core::HealthBar, 1> bars{ { { .entity = FOX, .previous = {}, .current = {}, .fraction = 1.0F } } };
    const core::Manifest manifest = TestManifest();
    core::Effects tuned = EFFECTS;
    tuned.healthBar.width = EFFECTS.healthBar.width * DOUBLE;
    tuned.healthBar.rise = RISE;
    tuned.poisonLance.endScale = EFFECTS.poisonLance.endScale * DOUBLE;
    gfx::VolleyDrawing drawing;
    gfx::DrawHealthBars(bars, Whole(bars), 1.0F, manifest, {}, tuned, drawing);
    REQUIRE(drawing.instances.size() == 2);
    CHECK(drawing.instances[0].current.y == -RISE);
    CHECK(
        drawing.instances[0].size ==
        core::PackPair(core::PackHalf(tuned.healthBar.width), core::PackHalf(EFFECTS.healthBar.height))
    );
    const std::array<core::Flash, 1> flashes{
        { { .point = {}, .startTick = 0, .ticks = 10, .element = core::HitKind::Poison } }
    };
    gfx::VolleyDrawing burst;
    gfx::DrawFlashes(flashes, LAST_MOMENT, manifest, {}, tuned, burst);
    REQUIRE(burst.instances.size() == 1);
    gfx::VolleyDrawing plain;
    gfx::DrawFlashes(flashes, LAST_MOMENT, manifest, {}, EFFECTS, plain);
    CHECK(burst.instances[0].size != plain.instances[0].size);
}

} // namespace
