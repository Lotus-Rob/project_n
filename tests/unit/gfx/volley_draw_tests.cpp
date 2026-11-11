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

using core::atlas::SpriteId;

constexpr u64 START = 600;
constexpr u64 SEED = 0x5eed'0123'4567'89abULL;
constexpr core::Vec2 ORIGIN{ .x = 100.0F, .y = 50.0F };
constexpr core::Vec2 POINT{ .x = 700.0F, .y = -100.0F };
constexpr core::StableId FOX{ 9 };
// The shipped swords (assets/data/abilities.yaml) in ticks, as their cast resolves them.
constexpr core::VolleyShape SWORDS{
    .count = 1'000,
    .ringRadius = 180.0F,
    .staggerTicks = 18,
    .outTicks = 15,
    .flightTicks = 66,
    .sweep = 420.0F,
    .fadeTicks = 18,
    .commitTicks = core::CommitTicksFor(260.0F),
};
constexpr core::Volley VOLLEY{ .use = 7, .startTick = START, .seed = SEED, .origin = ORIGIN, .point = POINT, .shape = SWORDS };
// A fire bolt: one straight flight from the caster.
constexpr core::Volley BOLT{ .use = 8,
                             .startTick = START,
                             .seed = SEED,
                             .origin = ORIGIN,
                             .point = POINT,
                             .shape = { .count = 1, .flightTicks = 60, .commitTicks = core::MIN_COMMIT_TICKS } };
constexpr std::array<u32, 4> SAMPLED_SWORDS{ { 0, 1, 499, 999 } };
constexpr u64 MID_FLIGHT = START + 50; // every sword launched, none arrived
constexpr f32 CLOSE = 1e-4F;           // relative
constexpr f32 HALFWAY = 0.5F;          // of a tick, as the frame's alpha
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
    u32 workers = 1
) {
    core::Jobs jobs{ workers };
    gfx::VolleyDrawing drawing;
    gfx::DrawVolleys(volleys, targets, tick, alpha, TestManifest(), colours, jobs, drawing);
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
        const gfx::VolleyDrawing drawing = Drawn(volleys, targets, MID_FLIGHT, alpha);
        const f64 renderTick = core::RenderTick(MID_FLIGHT, static_cast<f64>(alpha));
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
    u32 waiting = 0;
    for (u32 sword = 0; sword < SWORDS.count; ++sword) {
        if (static_cast<f64>(core::LaunchTick(VOLLEY, sword)) <= core::RenderTick(TICK, static_cast<f64>(HALFWAY))) {
            continue;
        }
        ++waiting;
        CHECK(IsZero(SwordOf(drawing, sword)));
        CHECK(IsZero(HaloOf(drawing, sword)));
        for (u32 quad = 0; quad < gfx::TRAIL_SEGMENTS; ++quad) {
            for (u32 corner = 0; corner < gfx::QUAD_VERTICES; ++corner) {
                CHECK(TrailVertex(drawing, sword, quad, corner).colour == 0);
            }
        }
    }
    CHECK(waiting > 0);
}

TEST_CASE("the trail is sampled from the same path a little back in time") {
    const std::array<core::VolleyTarget, 1> targets{ { { .entity = FOX, .previous = POINT, .current = POINT } } };
    const std::array<core::VolleyView, 1> volleys{ { SwordsView() } };
    constexpr f32 ALPHA = HALFWAY;
    const gfx::VolleyDrawing drawing = Drawn(volleys, targets, MID_FLIGHT, ALPHA);
    const f64 renderTick = core::RenderTick(MID_FLIGHT, static_cast<f64>(ALPHA));
    constexpr u32 LAST = gfx::TRAIL_SEGMENTS - 1;
    for (const u32 sword : SAMPLED_SWORDS) {
        CAPTURE(sword);
        const core::SwordPose head = core::SwordAt(VOLLEY, sword, renderTick, Spot(POINT, sword));
        const core::SwordPose tail =
            core::SwordAt(VOLLEY, sword, renderTick - static_cast<f64>(gfx::TRAIL_TICKS), Spot(POINT, sword));
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
    gfx::DrawHealthBars(bars, Whole(bars), 1.0F, TestManifest(), {}, drawing);
    gfx::DrawVolleys(volleys, targets, MID_FLIGHT, 1.0F, TestManifest(), {}, jobs, drawing);
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

TEST_CASE("swords past the overlay's room are left out whole, never a halo or a core without its sword") {
    // Two of the largest volleys: more swords than the overlay holds three instances of.
    core::VolleyView largest = SwordsView();
    largest.volley.shape.count = core::MAX_VOLLEY_COUNT;
    const std::array<core::VolleyView, 2> volleys{ { largest, largest } };
    const std::array<core::VolleyTarget, 1> targets{ { { .entity = FOX, .previous = POINT, .current = POINT } } };
    const std::array<core::HealthBar, 1> bars{ { { .entity = FOX, .previous = POINT, .current = POINT, .fraction = HALF } } };
    constexpr u32 BAR_INSTANCES = 2;
    constexpr u32 FITTING = (gfx::MAX_OVERLAY_INSTANCES - BAR_INSTANCES) / gfx::SWORD_INSTANCES;
    core::Jobs jobs{ 1 };
    gfx::VolleyDrawing drawing;
    gfx::DrawHealthBars(bars, Whole(bars), 1.0F, TestManifest(), {}, drawing);
    gfx::DrawVolleys(volleys, targets, MID_FLIGHT, 1.0F, TestManifest(), {}, jobs, drawing);
    CHECK(drawing.instances.size() <= gfx::MAX_OVERLAY_INSTANCES);
    REQUIRE(drawing.batches.size() == 4);
    CHECK(drawing.batches[0].count == FITTING);
    CHECK(drawing.batches[1].count == FITTING);
    CHECK(drawing.batches[3].count == FITTING);
    CHECK(drawing.droppedSwords == (2 * core::MAX_VOLLEY_COUNT) - FITTING);
    CHECK(drawing.ribbons.size() == std::size_t{ FITTING } * gfx::TRAIL_SEGMENTS * gfx::QUAD_VERTICES);
    drawing.Clear();
    CHECK(drawing.droppedSwords == 0);
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
    for (const u64 tick : { START + 3, MID_FLIGHT, START + 85, START + 100 }) {
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
    constexpr core::Vec2 MIDDLE{ .x = 110.0F, .y = 210.0F - gfx::HEALTH_BAR_RISE }; // halfway, risen
    const std::array<core::HealthBar, 1> bars{
        { { .entity = FOX, .previous = BEFORE, .current = AFTER, .fraction = FRACTION } }
    };
    const core::Manifest manifest = TestManifest();
    gfx::VolleyDrawing drawing;
    gfx::DrawHealthBars(bars, Whole(bars), HALFWAY, manifest, {}, drawing);
    REQUIRE(drawing.instances.size() == 2);
    const core::Instance& background = drawing.instances[0];
    const core::Instance& fill = drawing.instances[1];
    CheckAt(background.current, MIDDLE);
    CHECK(background.size == core::PackPair(core::PackHalf(gfx::HEALTH_BAR_WIDTH), core::PackHalf(gfx::HEALTH_BAR_HEIGHT)));
    const f32 length = gfx::HEALTH_FILL_WIDTH * FRACTION;
    CHECK(fill.size == core::PackPair(core::PackHalf(length), core::PackHalf(gfx::HEALTH_FILL_HEIGHT)));
    // From the left of the full fill's place.
    CheckAt(fill.current, { .x = MIDDLE.x - (gfx::HEALTH_FILL_WIDTH * HALF) + (length * HALF), .y = MIDDLE.y });
    const core::UiColours defaults{};
    CHECK(background.colour == defaults.Of(core::UiColour::BarBackground));
    CHECK(fill.colour == defaults.Of(core::UiColour::EnemyHealthBar));

    constexpr u32 RECOLOURED_FILL = 0xFF00'FF00U;
    constexpr u32 RECOLOURED_BACK = 0xFF40'2010U;
    core::UiColours recoloured;
    recoloured.Set(core::UiColour::EnemyHealthBar, RECOLOURED_FILL);
    recoloured.Set(core::UiColour::BarBackground, RECOLOURED_BACK);
    drawing.Clear();
    gfx::DrawHealthBars(bars, Whole(bars), HALFWAY, manifest, recoloured, drawing);
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
    gfx::DrawHealthBars(bars, Whole(bars), 1.0F, TestManifest(), {}, drawing);
    REQUIRE(drawing.instances.size() == 4);
    const u32 full = core::PackPair(core::PackHalf(gfx::HEALTH_FILL_WIDTH), core::PackHalf(gfx::HEALTH_FILL_HEIGHT));
    const u32 empty = core::PackPair(core::PackHalf(0.0F), core::PackHalf(gfx::HEALTH_FILL_HEIGHT));
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
    gfx::DrawHealthBars(bars, shown, 1.0F, TestManifest(), {}, drawing);
    REQUIRE(drawing.instances.size() == 2); // only the first's
    const core::UiColours defaults{};
    CHECK(drawing.instances[0].colour == Times(defaults.Of(core::UiColour::BarBackground), FADING));
    CHECK(drawing.instances[1].colour == Times(defaults.Of(core::UiColour::EnemyHealthBar), FADING));
}

TEST_CASE("cores, halos and trails take their element's colour, not their ability's") {
    const std::array<core::VolleyTarget, 2> targets{ { { .entity = FOX, .previous = POINT, .current = POINT },
                                                       { .entity = FOX, .previous = POINT, .current = POINT } } };
    const std::array<core::VolleyView, 2> volleys{ { SwordsView(), BoltView(1) } };
    constexpr u64 TICK = START + 40; // the bolt mid-flight, every sword shown with its whole trail
    constexpr u32 SWORD = 0;
    constexpr u32 THE_BOLT = SWORDS.count;
    REQUIRE(
        static_cast<f64>(core::LaunchTick(VOLLEY, SWORD)) < core::RenderTick(TICK, 1.0) - static_cast<f64>(gfx::TRAIL_TICKS)
    );
    const core::Manifest manifest = TestManifest();
    const auto check = [&](const core::UiColours& colours) {
        const gfx::VolleyDrawing drawing = Drawn(volleys, targets, TICK, 1.0F, colours);
        const u32 damage = colours.Of(core::UiColour::DamageEffect);
        const u32 burn = colours.Of(core::UiColour::BurnEffect);
        CHECK(CoreOf(drawing, SWORD).colour == Times(damage, gfx::CORE_OPACITY));
        CHECK(HaloOf(drawing, SWORD).colour == Times(damage, gfx::HALO_INTENSITY));
        CHECK(TrailVertex(drawing, SWORD, 0, 0).colour == Times(damage, gfx::HALO_INTENSITY));
        CHECK(SwordOf(drawing, SWORD).colour == core::OPAQUE_WHITE); // its own steel
        CHECK(CoreOf(drawing, THE_BOLT).colour == Times(burn, gfx::CORE_OPACITY));
        CHECK(HaloOf(drawing, THE_BOLT).colour == Times(burn, gfx::HALO_INTENSITY));
        CHECK(TrailVertex(drawing, THE_BOLT, 0, 0).colour == Times(burn, gfx::HALO_INTENSITY));
        CHECK(SwordOf(drawing, THE_BOLT).colour == burn); // white art, tinted
        // The ability picks the sword's sprite; every core is the same dot.
        CHECK(SwordOf(drawing, SWORD).regionMin == core::MakeInstance(manifest, { .sprite = SpriteId::Sword }).regionMin);
        CHECK(SwordOf(drawing, THE_BOLT).regionMin == core::MakeInstance(manifest, { .sprite = SpriteId::FireBolt }).regionMin);
        CHECK(CoreOf(drawing, SWORD).regionMin == core::MakeInstance(manifest, { .sprite = SpriteId::GlowCore }).regionMin);
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
    CHECK(CoreOf(after, THE_BOLT).colour == Times(GREEN, gfx::CORE_OPACITY));
    CHECK(HaloOf(after, THE_BOLT).colour == GREEN);
}

} // namespace
