#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/glow.hpp"
#include "core/manifest.hpp"
#include "core/seal.hpp"
#include "core/seals.hpp"
#include "core/tick_clock.hpp"
#include "core/ui_colours.hpp"
#include "core/vec2.hpp"
#include "gfx/mesh_vertex.hpp"
#include "gfx/seal_draw.hpp"
#include "gfx/sprite_block.hpp"

#include <doctest/doctest.h>

namespace {

using core::atlas::SpriteId;

constexpr f32 VIEW_RATIO = 0.375F;
constexpr f32 CLOSE = 1e-3F;       // world units, or a fraction of full opacity
constexpr f32 CLOSE_UV = 1e-6F;    // of a page
constexpr f32 CLOSE_ANGLE = 1e-5F; // radians: well under what an f32 time of 100 hours would lose
constexpr f64 TICKS = static_cast<f64>(core::TICKS_PER_SECOND);
constexpr f64 FOREVER = std::numeric_limits<f64>::infinity();
constexpr f64 DEGREES_A_TURN = 360.0;
constexpr f32 HALF_TURN = 180.0F;
constexpr f32 QUARTER_TURN = 90.0F;
constexpr core::Vec2 POINT{ .x = 300.0F, .y = 200.0F };
constexpr f32 SIZE = 100.0F; // every seal's, so a ring's radius is its fraction of 100
constexpr f32 HEIGHT = 20.0F;
constexpr f32 LINE = 0.01F; // a part's width
constexpr f32 HALF_PI = std::numbers::pi_v<f32> / 2.0F;
constexpr core::Seal SEAL{ .key = 1, .point = POINT, .size = SIZE };
constexpr f64 SECOND = 1.0; // when most cases draw

f32 Distance(core::Vec2 lhs, core::Vec2 rhs) {
    return std::hypot(lhs.x - rhs.x, lhs.y - rhs.y);
}

// The point of a ring SIZE across at `degrees` on a seal's plane, `height` off it.
gfx::SealLocal Ring(f32 degrees, f32 height) {
    const f32 radians = degrees * std::numbers::pi_v<f32> / HALF_TURN;
    return { .x = SIZE * std::cos(radians), .depth = SIZE * std::sin(radians), .up = height };
}

// A manifest of every sprite, 24 by 32 texels, each at its own place past the field's reach; the runes on page 1.
core::Manifest TestManifest() {
    constexpr u32 PAGE_SIZE = 1'024;
    constexpr u16 STRIDE = 70;
    constexpr u16 MARGIN = 40;
    constexpr std::size_t ROW = 12;
    constexpr u16 WIDTH = 24;
    constexpr u16 HEIGHT_TEXELS = 32;
    std::vector<core::SpriteInfo> sprites;
    for (std::size_t sprite = 0; sprite < core::atlas::SPRITE_NAMES.size(); ++sprite) {
        const bool rune = sprite >= std::to_underlying(SpriteId::Rune00) && sprite <= std::to_underlying(SpriteId::Rune23);
        sprites.push_back(
            { .file = "sprites/test.png",
              .page = static_cast<u16>(rune ? 1 : 0),
              .x = static_cast<u16>(MARGIN + ((sprite % ROW) * STRIDE)),
              .y = static_cast<u16>(MARGIN + ((sprite / ROW) * STRIDE)),
              .width = WIDTH,
              .height = HEIGHT_TEXELS }
        );
    }
    return { .pageSize = PAGE_SIZE, .pages = 2, .mipLevels = 1, .sprites = std::move(sprites), .scenes = {} };
}

// One round layer from `inner` to `outer`, turning at `spin`, with a circle at its edge.
core::SealLayer Layer(f32 inner, f32 outer, f32 spin = 0.0F) {
    return { .inner = inner,
             .outer = outer,
             .spin = spin,
             .parts = { { .kind = core::SealPartKind::Circle, .radius = outer, .width = LINE } } };
}

// A ground seal of one still layer, the outer half, in the element's colour.
core::SealLook OneLayer() {
    constexpr f32 INNER = 0.5F;
    core::SealLook look;
    look.colours = { { { .element = true } } };
    look.layers = { Layer(INNER, 1.0F) };
    return look;
}

// The three layers' life: appearing over 0.3 s, a share of 0.1 s each; fading over the last 0.4 s.
constexpr f64 APPEAR = 0.3;
constexpr f64 SHARE = 0.1;
constexpr f64 FADE = 0.4;
constexpr f64 HALF_FADE = 0.2;
constexpr f64 HELD = 10.0; // long after every layer appeared
// Their edges, outermost first.
constexpr std::array<f32, 4> EDGES{ { 1.0F, 0.7F, 0.4F, 0.0F } };

// Three layers listed outermost first, fading along `fadeCurve`, appearing outer or inner first.
core::SealLook ThreeLayers(core::GlowCurve fadeCurve, bool innerFirst) {
    core::SealLook look = OneLayer();
    look.appear = APPEAR;
    look.appearCurve = core::GlowCurve::Linear;
    look.fade = FADE;
    look.fadeCurve = fadeCurve;
    look.innerFirst = innerFirst;
    look.layers = { Layer(EDGES[1], EDGES[0]), Layer(EDGES[2], EDGES[1]), Layer(EDGES[3], EDGES[2]) };
    return look;
}

// A table of `looks` by name, sorted by key as the reader leaves it.
core::Seals Table(std::vector<std::pair<std::string_view, core::SealLook>> looks) {
    core::Seals seals;
    for (auto& [name, look] : looks) {
        seals.looks.emplace_back(core::SealKeyOf(name), std::move(look));
    }
    std::ranges::sort(seals.looks, {}, &std::pair<core::SealKey, core::SealLook>::first);
    return seals;
}

// One seal of the look `name` at POINT.
std::array<core::Seal, 1> One(std::string_view name, bool showcase = false) {
    return { { { .key = core::SealKeyOf(name), .point = POINT, .size = SIZE, .showcase = showcase } } };
}

// Draws `seals` at `seconds` by `looks` packed, the showcase shown when `showcase`.
gfx::SealDrawing Draw(std::span<const core::Seal> seals, const core::Seals& looks, f64 seconds, bool showcase = true) {
    const gfx::SealPack pack = gfx::PackSeals(looks, TestManifest());
    gfx::SealDrawing out;
    gfx::DrawSeals(seals, looks, pack, core::UiColours{}, seconds, static_cast<u64>(seconds * TICKS), showcase, out);
    return out;
}

TEST_CASE("a ground seal lies flat at the art's angle") {
    constexpr f32 SQUASHED = SIZE * VIEW_RATIO;
    // At 0°, 90°, 180° and 270°: the ring's ends across, then its nearest and farthest points, squashed by the ratio.
    constexpr std::array<core::Vec2, 4> AWAY{ { { .x = SIZE }, { .y = SQUASHED }, { .x = -SIZE }, { .y = -SQUASHED } } };
    f32 worst = 0.0F;
    for (std::size_t quarter = 0; quarter < AWAY.size(); ++quarter) {
        const gfx::SealLocal local = Ring(QUARTER_TURN * static_cast<f32>(quarter), 0.0F);
        const core::Vec2 lands = gfx::SealToWorld(SEAL, core::SealPlane::Ground, 0.0F, VIEW_RATIO, local);
        worst = std::max(worst, Distance(lands, POINT + AWAY.at(quarter)));
    }
    CHECK(worst < CLOSE);
    // A layer 20 high rises 20 up the screen.
    const core::Vec2 raised = gfx::SealToWorld(SEAL, core::SealPlane::Ground, 0.0F, VIEW_RATIO, Ring(0.0F, HEIGHT));
    CHECK(Distance(raised, POINT + core::Vec2{ .x = SIZE, .y = -HEIGHT }) < CLOSE);
}

TEST_CASE("an upright seal stands as a circle and its heights come forward") {
    // Its plane faces the viewer: 90° round its ring is a whole radius down, not squashed.
    const core::Vec2 side = gfx::SealToWorld(SEAL, core::SealPlane::Upright, 0.0F, VIEW_RATIO, Ring(QUARTER_TURN, 0.0F));
    CHECK(Distance(side, POINT + core::Vec2{ .y = SIZE }) < CLOSE);
    // A layer 20 high stands 20 nearer the viewer, which the art's angle shows 7.5 lower.
    const core::Vec2 forward = gfx::SealToWorld(SEAL, core::SealPlane::Upright, 0.0F, VIEW_RATIO, Ring(0.0F, HEIGHT));
    CHECK(Distance(forward, POINT + core::Vec2{ .x = SIZE, .y = HEIGHT * VIEW_RATIO }) < CLOSE);
}

TEST_CASE("an overhead seal is lifted by its lift") {
    constexpr f32 LIFT = 50.0F;
    const core::Vec2 lifted = gfx::SealToWorld(SEAL, core::SealPlane::Overhead, LIFT, VIEW_RATIO, Ring(0.0F, 0.0F));
    CHECK(Distance(lifted, POINT + core::Vec2{ .x = SIZE, .y = -LIFT }) < CLOSE);
}

TEST_CASE("layers appear one after another and fade together") {
    const core::SealLook outerFirst = ThreeLayers(core::GlowCurve::Linear, false);
    // Each layer appears over its share: after one share the outermost has, the innermost hasn't begun.
    CHECK(gfx::SealLifeOpacity(outerFirst, 0, SHARE, FOREVER) == doctest::Approx(1.0F));
    CHECK(gfx::SealLifeOpacity(outerFirst, 2, SHARE, FOREVER) == 0.0F);
    // Inner first, the innermost (listed last) is the one that has.
    const core::SealLook innerFirst = ThreeLayers(core::GlowCurve::Linear, true);
    CHECK(gfx::SealLifeOpacity(innerFirst, 2, SHARE, FOREVER) == doctest::Approx(1.0F));
    // Halfway through the fade every layer is at half, whenever it appeared.
    constexpr f32 HALF = 0.5F;
    f32 worst = 0.0F;
    for (std::size_t layer = 0; layer < outerFirst.layers.size(); ++layer) {
        worst = std::max(worst, std::abs(gfx::SealLifeOpacity(outerFirst, layer, HELD, HALF_FADE) - HALF));
    }
    CHECK(worst < CLOSE);
    // The fade takes its curve as the glows' does, over the share gone by: ease in has lost only a quarter halfway.
    constexpr f32 THREE_QUARTERS = 0.75F;
    const core::SealLook easeIn = ThreeLayers(core::GlowCurve::EaseIn, false);
    CHECK(gfx::SealLifeOpacity(easeIn, 1, HELD, HALF_FADE) == doctest::Approx(THREE_QUARTERS));
}

TEST_CASE("a layer turns at its spin, scaled by spin_scale") {
    constexpr f64 LONG_AFTER = 1'000.0;
    const core::SealLayer layer = Layer(EDGES[1], EDGES[0], -QUARTER_TURN);
    CHECK(gfx::SealLayerAngle(layer, SECOND, 1.0F) == doctest::Approx(-HALF_PI));
    // Reduce motion's still: no turn however long it has been up.
    CHECK(gfx::SealLayerAngle(layer, LONG_AFTER, 0.0F) == 0.0F);
}

TEST_CASE("a satellite on a turning ring follows it") {
    constexpr f32 RING_INNER = 0.8F;
    constexpr f32 MIDDLE = 90.0F; // the ring's band runs from 0.8 to 1 of the seal's 100
    constexpr f32 MOON = 0.1F;
    constexpr f32 ORBIT = 45.0F;
    core::SealLook ring = OneLayer();
    ring.layers = { Layer(RING_INNER, 1.0F, QUARTER_TURN) };
    const core::SealKey moon = core::SealKeyOf("moon");
    ring.satellites = { { .seal = moon, .size = MOON, .mount = core::SealMount::WithLayer },
                        { .seal = moon, .size = MOON, .mount = core::SealMount::Fixed },
                        { .seal = moon, .size = MOON, .mount = core::SealMount::Orbit, .orbit = ORBIT } };
    const gfx::SealDrawing drawn = Draw(One("ring"), Table({ { "ring", ring }, { "moon", OneLayer() } }), SECOND);
    REQUIRE(drawn.layers.size() == 4);
    // After a second the ring has turned 90°: the satellite on it is at its nearest point.
    CHECK(Distance(drawn.layers[1].centre, POINT + core::Vec2{ .y = MIDDLE * VIEW_RATIO }) < CLOSE);
    // A fixed one stays at 0°.
    CHECK(Distance(drawn.layers[2].centre, POINT + core::Vec2{ .x = MIDDLE }) < CLOSE);
    // An orbiting one has gone its own 45°.
    const f32 diagonal = MIDDLE * std::numbers::sqrt2_v<f32> / 2.0F;
    CHECK(Distance(drawn.layers[3].centre, POINT + core::Vec2{ .x = diagonal, .y = diagonal * VIEW_RATIO }) < CLOSE);
}

// The middle of the first quad's outer edge (a quad's corners: inner start, inner end, outer start, outer end) of `band`.
core::Vec2 OuterMiddle(std::span<const gfx::SealVertex> band) {
    constexpr f32 MIDWAY = 0.5F;
    return (band[2].local + band[3].local) * MIDWAY;
}

TEST_CASE("a frame's band reaches its corners, its sides' middles nearer") {
    constexpr u32 SQUARE = 4;
    // Its outer radius, and the margin for the shader's halo with the table's default reach.
    constexpr f32 REACH = 1.0F + gfx::SealBandMargin(core::shipped::SEAL_HALO_REACH);
    core::SealLook look = OneLayer();
    look.layers = { { .inner = 0.0F, .outer = 1.0F, .frameSides = SQUARE, .frameRotation = 0.0F, .parts = {} } };
    const gfx::SealDrawing drawn = Draw(One("frame"), Table({ { "frame", look } }), SECOND);
    REQUIRE(drawn.groundLines.size() == std::size_t{ SQUARE } * gfx::QUAD_VERTICES);
    // A frame's outer radius is its corners'; a square's sides' middles are only cos(45°) of it out.
    const f32 corner = Distance(drawn.groundLines[2].local, {});
    const f32 middle = Distance(OuterMiddle(drawn.groundLines), {});
    CHECK(std::max(std::abs(corner - REACH), std::abs(middle - (REACH * std::cos(std::numbers::pi_v<f32> / SQUARE)))) < CLOSE);
    // Its inner corners are at the centre, with no way in to grow.
    CHECK(drawn.groundLines[0].grow == doctest::Approx(0.0F));
    CHECK(drawn.groundLines[2].grow == doctest::Approx(1.0F));
}

TEST_CASE("a round band holds its outer circle and the wash reaches its reach") {
    constexpr f32 REACH = core::shipped::SEAL_WASH_REACH;
    const gfx::SealDrawing drawn = Draw(One("round"), Table({ { "round", OneLayer() } }), SECOND);
    REQUIRE((drawn.groundLines.size() >= gfx::QUAD_VERTICES && drawn.groundWash.size() == gfx::QUAD_VERTICES));
    // Its quads' outer edges are chords outside the circle: the nearest point of one is still the outer radius out, and the
    // margin for the shader's halo past it; its inner edge is the margin inside the inner radius.
    constexpr f32 MARGIN = gfx::SealBandMargin(core::shipped::SEAL_HALO_REACH);
    const f32 outerReach = Distance(OuterMiddle(drawn.groundLines), {});
    CHECK(outerReach >= 1.0F + MARGIN - CLOSE_UV);
    CHECK(Distance(drawn.groundLines[0].local, {}) == doctest::Approx(OneLayer().layers[0].inner - MARGIN));
    // The vertex shader grows a band a pixel past both edges: its outer corners out, its inner ones in; never its wash.
    CHECK(drawn.groundLines[0].grow == doctest::Approx(-1.0F));
    CHECK(drawn.groundLines[2].grow == doctest::Approx(1.0F));
    CHECK(drawn.groundWash[0].grow == doctest::Approx(0.0F));
    // The wash's far corner is its reach out each way, squashed by the art's angle.
    const core::Vec2 far = POINT + core::Vec2{ .x = REACH * SIZE, .y = REACH * SIZE * VIEW_RATIO };
    CHECK(Distance(drawn.groundWash[3].position, far) < CLOSE);
}

TEST_CASE("an overhead seal's shadow lies on the ground under it, as light as its look says; none without one") {
    constexpr f32 LIFT = 120.0F;
    constexpr f32 SHADOW = 0.2F;
    constexpr f32 REACH = core::shipped::SEAL_WASH_REACH;
    core::SealLook overhead = OneLayer();
    overhead.plane = core::SealPlane::Overhead;
    overhead.lift = LIFT;
    core::SealLook shaded = overhead;
    shaded.washShadow = SHADOW;
    const gfx::SealDrawing drawn = Draw(One("shaded"), Table({ { "shaded", shaded } }), HELD);
    // Its wash stays in its own plane, overhead; the shadow is the only quad on the ground.
    CHECK(drawn.aboveWash.size() == gfx::QUAD_VERTICES);
    REQUIRE(drawn.groundWash.size() == gfx::QUAD_VERTICES);
    // Its far corner is the wash's reach out each way on the ground, not lifted.
    const core::Vec2 far = POINT + core::Vec2{ .x = REACH * SIZE, .y = REACH * SIZE * VIEW_RATIO };
    CHECK(Distance(drawn.groundWash[3].position, far) < CLOSE);
    const gfx::SealGpuLayer& shadow = drawn.layers.at(drawn.groundWash[0].layer);
    CHECK(Distance(shadow.centre, POINT) < CLOSE);
    CHECK(shadow.washOpacity == doctest::Approx(SHADOW)); // whole, under the opacity cap
    const gfx::SealGpuLayer& wash = drawn.layers.at(drawn.aboveWash[0].layer);
    CHECK(shadow.washColour == wash.washColour); // the wash's ink
    CHECK(shadow.washReach == wash.washReach);

    const gfx::SealDrawing plain = Draw(One("plain"), Table({ { "plain", overhead } }), HELD);
    CHECK(plain.groundWash.empty());
}

TEST_CASE("a look's parts are packed sorted by where their bands start, and its drawn layer points at them") {
    // Where each band starts: a star's at its apothem (0.9 cos 60°, less half its line), well inside its radius, so it
    // goes before the ticks it lies outside of; the circle and the star share a radius but not a start.
    constexpr f32 OUTER = 0.9F;
    constexpr f32 MIDDLE = 0.6F;
    constexpr f32 INNER = 0.3F;
    constexpr f32 DOT = 0.02F;
    constexpr u32 COUNT = 8;
    constexpr u32 POINTS = 6;
    constexpr u32 STEP = 2; // a hexagram
    core::SealLook look = OneLayer();
    look.layers[0].parts = {
        { .kind = core::SealPartKind::Circle, .radius = OUTER, .width = LINE },
        { .kind = core::SealPartKind::Ticks, .radius = MIDDLE, .width = LINE, .size = DOT, .count = COUNT },
        { .kind = core::SealPartKind::Dots, .radius = INNER, .size = DOT, .count = COUNT },
        { .kind = core::SealPartKind::Star, .radius = OUTER, .width = LINE, .count = POINTS, .step = STEP },
    };
    const core::Seals looks = Table({ { "sorted", look } });
    const gfx::SealPack pack = gfx::PackSeals(looks, TestManifest());
    constexpr std::array<u32, 4> SORTED{ { std::to_underlying(core::SealPartKind::Dots),
                                           std::to_underlying(core::SealPartKind::Star),
                                           std::to_underlying(core::SealPartKind::Ticks),
                                           std::to_underlying(core::SealPartKind::Circle) } };
    const bool sorted = std::ranges::equal(pack.parts, SORTED, {}, &gfx::SealGpuPart::kind);
    CHECK(sorted);
    // Each frame only points the layer at its look's packed parts.
    const gfx::SealDrawing drawn = Draw(One("sorted"), looks, SECOND);
    REQUIRE(drawn.layers.size() == 1);
    CHECK((drawn.layers[0].firstPart == pack.layers[0].firstPart && drawn.layers[0].partCount == SORTED.size()));
}

TEST_CASE("an element slot takes the record's element colour and a named slot the table's") {
    core::SealLook look = OneLayer();
    look.colours = { { { .element = true }, { .colour = core::UiColour::SealGold } } };
    look.colourCount = 2;
    std::array<core::Seal, 1> seals = One("two");
    seals[0].element = core::HitKind::Poison;
    seals[0].hasElement = true;
    const gfx::SealDrawing drawn = Draw(seals, Table({ { "two", look } }), SECOND);
    REQUIRE(drawn.layers.size() == 1);
    const core::UiColours colours{};
    CHECK(drawn.layers[0].colours[0] == colours.Of(core::UiColour::PoisonEffect));
    CHECK(drawn.layers[0].colours[1] == colours.Of(core::UiColour::SealGold));
}

TEST_CASE("brightness scales a seal's lines and light, and the above cap holds after it") {
    constexpr f32 CAP = 0.7F;
    constexpr f32 BRIGHT = 2.0F;
    constexpr f32 DIM = 0.5F;
    constexpr f32 EMBLEM = 0.2F;
    constexpr core::SealLight SPARKS{ .sparks = 8, .sparkRise = 40.0F, .sparkLife = 1.0, .sparkSize = 0.02F };
    core::SealLook look = OneLayer();
    look.emblem = core::SealEmblem{ .sprite = SpriteId::EmblemSwords, .size = EMBLEM };
    look.light = SPARKS;
    core::SealLook overhead = look;
    overhead.plane = core::SealPlane::Overhead;
    core::Seals looks = Table({ { "ground", look }, { "overhead", overhead } });
    looks.aboveOpacity = CAP;
    // Each strength drawn: the layer's, the emblem's and the light's.
    const auto strengths = [&looks](std::string_view name, f32 brightness) {
        looks.brightness = brightness;
        const gfx::SealDrawing drawn = Draw(One(name), looks, HELD);
        REQUIRE(drawn.layers.size() == 2);
        const std::vector<gfx::SealGpuParticles>& lights =
            drawn.groundParticles.empty() ? drawn.aboveParticles : drawn.groundParticles;
        REQUIRE(lights.size() == 1);
        return std::array<f32, 3>{ { drawn.layers[0].opacity, drawn.layers[1].opacity, lights[0].opacity } };
    };
    // Raised, brighter than the cap: capped after brightness, so the fight stays readable.
    constexpr std::array<f32, 3> ALL_CAPPED{ { CAP, CAP, CAP } };
    const std::array<f32, 3> capped = strengths("overhead", BRIGHT);
    CHECK(capped == ALL_CAPPED);
    // Raised and dimmed below the cap: dimmed from its full strength, not from the cap.
    constexpr std::array<f32, 3> ALL_DIMMED{ { DIM, DIM, DIM } };
    const std::array<f32, 3> dimmed = strengths("overhead", DIM);
    CHECK(dimmed == ALL_DIMMED);
    // On the ground there is no cap: brightness above 1 shows whole.
    constexpr std::array<f32, 3> ALL_BRIGHT{ { BRIGHT, BRIGHT, BRIGHT } };
    const std::array<f32, 3> ground = strengths("ground", BRIGHT);
    CHECK(ground == ALL_BRIGHT);
}

TEST_CASE("hidden showcase seals draw nothing") {
    const gfx::SealDrawing drawn = Draw(One("shown", true), Table({ { "shown", OneLayer() } }), SECOND, false);
    CHECK((drawn.layers.empty() && drawn.groundWash.empty() && drawn.groundLines.empty() && drawn.droppedSeals == 0));
}

TEST_CASE("the most seals a frame draws, each the largest an overhead seal with a shadow can be, fit the renderer's buffers") {
    // The worst case seals.yaml allows: every layer a frame of the most sides, an emblem, a wash and a shadow.
    core::SealLook largest;
    largest.plane = core::SealPlane::Overhead;
    largest.washShadow = 1.0F;
    largest.colours = { { { .element = true } } };
    for (u32 layer = 0; layer < core::MAX_SEAL_LAYERS; ++layer) {
        largest.layers.push_back({ .inner = 0.0F, .outer = 1.0F, .frameSides = core::MAX_SEAL_FRAME_SIDES, .parts = {} });
    }
    largest.emblem = core::SealEmblem{ .sprite = SpriteId::EmblemSwords, .size = 1.0F };
    const core::Seals looks = Table({ { "largest", largest } });
    const std::vector<core::Seal> seals(gfx::MAX_SEALS_DRAWN, One("largest").front());
    const gfx::SealDrawing drawn = Draw(seals, looks, HELD);
    CHECK(drawn.droppedSeals == 0);
    CHECK(drawn.layers.size() <= gfx::MAX_SEAL_LAYERS_DRAWN);
    const std::size_t quads =
        (drawn.groundWash.size() + drawn.groundLines.size() + drawn.aboveWash.size() + drawn.aboveLines.size()) /
        gfx::QUAD_VERTICES;
    CHECK(quads <= gfx::MAX_SEAL_QUADS);
    // Every quad's layer is one the layer buffer holds.
    u32 highest = 0;
    for (const std::vector<gfx::SealVertex>* list :
         { &drawn.groundWash, &drawn.groundLines, &drawn.aboveWash, &drawn.aboveLines }) {
        for (const gfx::SealVertex& vertex : *list) {
            highest = std::max(highest, vertex.layer);
        }
    }
    CHECK(highest < gfx::MAX_SEAL_LAYERS_DRAWN);
}

TEST_CASE("more seals than the frame holds are counted") {
    constexpr std::size_t SEALS = 70;
    const core::Seal many = One("many").front();
    const std::vector<core::Seal> seals(SEALS, many);
    const gfx::SealDrawing drawn = Draw(seals, Table({ { "many", OneLayer() } }), SECOND);
    CHECK(drawn.layers.size() == gfx::MAX_SEALS_DRAWN);
    CHECK(drawn.droppedSeals == SEALS - gfx::MAX_SEALS_DRAWN);
}

TEST_CASE("a full frame counts each seal it leaves out with every satellite it would have drawn") {
    // A planet with two moons, each with a speck, and a satellite on a ring it lacks (skipped, so never counted): five
    // seals a planet. Twelve planets and all but the last speck of a thirteenth fill the frame's 64; the rest are counted.
    constexpr std::size_t PLANETS = 20;
    constexpr std::size_t EACH = 5;
    constexpr f32 SMALLER = 0.2F; // each satellite's size
    core::SealLook speck = OneLayer();
    core::SealLook moon = OneLayer();
    moon.satellites = { { .seal = core::SealKeyOf("speck"), .size = SMALLER } };
    core::SealLook planet = OneLayer();
    planet.satellites = { { .seal = core::SealKeyOf("moon"), .size = SMALLER },
                          { .seal = core::SealKeyOf("moon"), .angle = HALF_TURN, .size = SMALLER },
                          { .seal = core::SealKeyOf("moon"), .layer = 1, .size = SMALLER } };
    const core::Seals looks = Table({ { "planet", planet }, { "moon", moon }, { "speck", speck } });
    const gfx::SealPack pack = gfx::PackSeals(looks, TestManifest());
    // Packed with the table: a planet draws five in play, three as a satellite (its specks too deep) and one deepest.
    std::size_t planetAt = 0;
    while (looks.looks.at(planetAt).first != core::SealKeyOf("planet")) {
        ++planetAt;
    }
    const std::array<u32, 3> counts{ { EACH, 3, 1 } };
    CHECK(pack.looks.at(planetAt).seals == counts);
    const std::vector<core::Seal> seals(PLANETS, One("planet").front());
    const gfx::SealDrawing drawn = Draw(seals, looks, SECOND);
    CHECK(drawn.layers.size() == gfx::MAX_SEALS_DRAWN);
    CHECK(drawn.droppedSeals == (PLANETS * EACH) - gfx::MAX_SEALS_DRAWN);
}

TEST_CASE("a seal buffer grows to twice its room or what is asked, never past a u32") {
    constexpr u32 SMALL = 1'000;
    constexpr std::size_t THRICE = 3'000;           // three times SMALL
    constexpr u32 LARGE = 3'000'000'000;            // twice it is past a u32
    constexpr u32 MOST = 4'294'967'295;             // the most a u32 counts
    constexpr std::size_t PAST = 4'294'967'296 + 8; // more bytes than a u32 counts, 8 once narrowed
    const std::optional<u32> held = gfx::SealBufferRoom(SMALL, SMALL);
    CHECK(held == SMALL);
    const std::optional<u32> doubled = gfx::SealBufferRoom(SMALL, SMALL + 1);
    CHECK(doubled == 2 * SMALL);
    const std::optional<u32> asked = gfx::SealBufferRoom(SMALL, THRICE);
    CHECK(asked == THRICE);
    // Doubling past a u32 stops at its most, never wrapping round to less.
    const std::optional<u32> capped = gfx::SealBufferRoom(LARGE, std::size_t{ LARGE } + 1);
    CHECK(capped == MOST);
    // Bytes no u32 counts have no room, rather than the few they narrow to.
    const std::optional<u32> past = gfx::SealBufferRoom(LARGE, PAST);
    CHECK_FALSE(past.has_value());
}

TEST_CASE("a never-ending seal hands the GPU small times") {
    // A spin whose turns an f32 of 100 hours couldn't hold to a hundredth of a degree.
    constexpr f32 SPIN = 7.3F;
    constexpr std::array<f64, 2> TIMES{ { 10.0, 100.0 * 3'600.0 } }; // seconds: soon, and 100 hours on
    constexpr core::SealLight LIGHT{ .pillars = 4,
                                     .pillarHeight = 80.0F,
                                     .pillarWidth = 0.05F,
                                     .pillarCycle = 1.7,
                                     .sparks = 24,
                                     .sparkRise = 40.0F,
                                     .sparkLife = 1.2,
                                     .shards = 6,
                                     .shardDrift = 15.0F,
                                     .shardSpin = 30.0F };
    core::SealLook look = OneLayer();
    look.layers[0].spin = SPIN;
    look.light = LIGHT;
    const core::Seals looks = Table({ { "forever", look } });
    f32 worstAge = 0.0F;  // of each stream's age over its period
    f32 worstTurn = 0.0F; // radians from the angle turned in f64
    for (const f64 seconds : TIMES) {
        const gfx::SealDrawing drawn = Draw(One("forever", true), looks, seconds);
        REQUIRE((drawn.groundParticles.size() == 1 && drawn.layers.size() == 1));
        const gfx::SealGpuParticles& light = drawn.groundParticles[0];
        for (const auto& [age, period] : { std::pair{ light.pillarAge, light.pillarCycle },
                                           std::pair{ light.sparkAge, light.sparkLife },
                                           std::pair{ light.shardAge, light.shardCycle } }) {
            worstAge = std::max(worstAge, age < 0.0F ? 1.0F : age / period);
        }
        const f64 turned =
            std::fmod(static_cast<f64>(SPIN) * seconds, DEGREES_A_TURN) * std::numbers::pi / static_cast<f64>(HALF_TURN);
        worstTurn = std::max(worstTurn, std::abs(drawn.layers[0].angle - static_cast<f32>(turned)));
    }
    CHECK(worstAge < 1.0F);
    CHECK(worstTurn < CLOSE_ANGLE);
}

// A light of every stream: 4 pillars, 24 sparks and 6 shards.
constexpr core::SealLight ALL_LIGHT{ .pillars = 4,
                                     .pillarHeight = 80.0F,
                                     .pillarWidth = 0.05F,
                                     .pillarFade = 0.5F,
                                     .pillarCycle = 1.7,
                                     .sparks = 24,
                                     .sparkRise = 40.0F,
                                     .sparkLife = 1.2,
                                     .sparkSize = 0.02F,
                                     .shards = 6,
                                     .shardDrift = 15.0F,
                                     .shardSpin = 30.0F,
                                     .shardSize = 0.05F };
constexpr u32 ALL_PARTICLES = 34;

TEST_CASE("a seal's particle count is its light's, capped") {
    constexpr u32 FLOOD = 300; // sparks past the limit, which only a look made in code can hold: the reader refuses it
    core::SealLook lit = OneLayer();
    lit.light = ALL_LIGHT;
    core::SealLook flood = lit;
    flood.light.sparks = FLOOD;
    core::SealLook above = lit;
    above.plane = core::SealPlane::Overhead;
    const core::Seal litSeal = One("lit").front();
    const core::Seal floodSeal = One("flood").front();
    const core::Seal aboveSeal = One("above").front();
    const std::vector<core::Seal> seals{ litSeal, floodSeal, litSeal, aboveSeal };
    const gfx::SealDrawing drawn = Draw(seals, Table({ { "lit", lit }, { "flood", flood }, { "above", above } }), SECOND);
    REQUIRE(drawn.groundParticles.size() == 3);
    const gfx::SealGpuParticles& first = drawn.groundParticles[0];
    CHECK((first.pillars == ALL_LIGHT.pillars && first.sparks == ALL_LIGHT.sparks && first.shards == ALL_LIGHT.shards));
    // Past the limit a seal keeps its pillars, then as many sparks as fit, and no shards; what it left out is counted.
    const gfx::SealGpuParticles& capped = drawn.groundParticles[1];
    CHECK(capped.pillars == ALL_LIGHT.pillars);
    CHECK(capped.sparks == core::MAX_SEAL_PARTICLES - ALL_LIGHT.pillars);
    CHECK(capped.shards == 0);
    CHECK(drawn.droppedParticles == std::size_t{ ALL_LIGHT.pillars + FLOOD + ALL_LIGHT.shards - core::MAX_SEAL_PARTICLES });
    // Each record's particles follow the one before's in its plane's list.
    CHECK(capped.firstParticle == ALL_PARTICLES);
    CHECK(drawn.groundParticles[2].firstParticle == ALL_PARTICLES + core::MAX_SEAL_PARTICLES);
    // An overhead seal's light is in the list drawn with the seals above, counted from its own first.
    REQUIRE(drawn.aboveParticles.size() == 1);
    CHECK(drawn.aboveParticles[0].firstParticle == 0);
}

// Every field of `particle`, bit for bit.
constexpr std::size_t PARTICLE_FIELDS = 9;
using ParticleBits = std::array<u32, PARTICLE_FIELDS>;
ParticleBits Bits(const gfx::SealParticle& particle) {
    return { { std::bit_cast<u32>(particle.centre.x),
               std::bit_cast<u32>(particle.centre.y),
               std::bit_cast<u32>(particle.along.x),
               std::bit_cast<u32>(particle.along.y),
               std::bit_cast<u32>(particle.across.x),
               std::bit_cast<u32>(particle.across.y),
               std::bit_cast<u32>(particle.strength),
               std::bit_cast<u32>(particle.phase),
               particle.kind } };
}

TEST_CASE("particles are placed the same for the same time") {
    // Three times a twentieth of a second apart, inside one life of each particle sampled (the seed is fixed).
    constexpr f64 STEP = 0.05;
    constexpr std::array<f64, 3> TIMES{ { SECOND, SECOND + STEP, SECOND + (2.0 * STEP) } };
    constexpr u32 PILLAR = 0;
    constexpr u32 SPARK = ALL_LIGHT.pillars;
    constexpr u32 SHARD = ALL_LIGHT.pillars + ALL_LIGHT.sparks;
    constexpr f32 HALF = 0.5F; // a quad's half-side of its size
    core::SealLook look = OneLayer();
    look.light = ALL_LIGHT;
    const core::Seals looks = Table({ { "lit", look } });
    std::array<std::array<gfx::SealParticle, 3>, TIMES.size()> placed{};
    for (std::size_t time = 0; time < TIMES.size(); ++time) {
        const gfx::SealDrawing drawn = Draw(One("lit"), looks, TIMES.at(time));
        REQUIRE(drawn.groundParticles.size() == 1);
        const gfx::SealGpuParticles& light = drawn.groundParticles[0];
        placed.at(time) = { gfx::SealParticleAt(light, PILLAR),
                            gfx::SealParticleAt(light, SPARK),
                            gfx::SealParticleAt(light, SHARD) };
    }
    // A second frame drawn at the same time, from records laid out afresh, places every particle bit for bit alike.
    const gfx::SealDrawing again = Draw(One("lit"), looks, TIMES[1]);
    REQUIRE(again.groundParticles.size() == 1);
    bool same = true;
    for (std::size_t sample = 0; sample < placed[1].size(); ++sample) {
        const u32 index = std::array<u32, 3>{ { PILLAR, SPARK, SHARD } }.at(sample);
        same = same && Bits(gfx::SealParticleAt(again.groundParticles[0], index)) == Bits(placed[1].at(sample));
    }
    CHECK(same);
    const auto& [pillar0, spark0, shard0] = placed[0];
    const auto& [pillar1, spark1, shard1] = placed[1];
    const auto& [pillar2, spark2, shard2] = placed[2];
    CHECK((pillar0.kind == gfx::SEAL_PILLAR && spark0.kind == gfx::SEAL_SPARK && shard0.kind == gfx::SEAL_SHARD));
    // A pillar stands where it stood, rising from the ground seal its height and its seal's width, its light moving on.
    CHECK(Distance(pillar0.centre, pillar2.centre) < CLOSE);
    CHECK(Distance(pillar0.along, { .y = -ALL_LIGHT.pillarHeight * HALF }) < CLOSE);
    CHECK(Distance(pillar0.across, { .x = ALL_LIGHT.pillarWidth * SIZE * HALF }) < CLOSE);
    const auto cycleStep = static_cast<f32>(STEP / ALL_LIGHT.pillarCycle);
    CHECK(pillar1.phase - pillar0.phase == doctest::Approx(cycleStep));
    // A spark rises at its rise, straight up the screen.
    const core::Vec2 risen = spark1.centre - spark0.centre;
    CHECK(Distance(risen, { .y = -ALL_LIGHT.sparkRise * static_cast<f32>(STEP) }) < CLOSE);
    CHECK(Distance(spark2.centre - spark1.centre, risen) < CLOSE);
    // A shard drifts steadily out from its seal's centre, turning as it goes.
    const core::Vec2 drifted = shard1.centre - shard0.centre;
    CHECK(Distance(shard2.centre - shard1.centre, drifted) < CLOSE);
    CHECK((drifted.x * (shard0.centre.x - POINT.x)) + (drifted.y * (shard0.centre.y - POINT.y)) > 0.0F);
    CHECK(Distance(shard0.along, shard1.along) > CLOSE);
}

TEST_CASE("a spark rises on through its stream's wrap and is born elsewhere a life later") {
    // Just before and just after the sparks' age wraps for the second time: the lap goes up with it, so the spark (inside
    // its own life there: the seed is fixed) moves on by its rise and doesn't jump.
    constexpr f64 WRAP = 2.0 * ALL_LIGHT.sparkLife;
    constexpr f64 EPSILON = 0.001;
    constexpr u32 SPARK = ALL_LIGHT.pillars;
    core::SealLook look = OneLayer();
    look.light = ALL_LIGHT;
    const core::Seals looks = Table({ { "lit", look } });
    const auto sparkAt = [&](f64 seconds) {
        const gfx::SealDrawing drawn = Draw(One("lit"), looks, seconds);
        REQUIRE(drawn.groundParticles.size() == 1);
        return gfx::SealParticleAt(drawn.groundParticles[0], SPARK);
    };
    const gfx::SealParticle before = sparkAt(WRAP - EPSILON);
    const gfx::SealParticle after = sparkAt(WRAP + EPSILON);
    const auto risen = ALL_LIGHT.sparkRise * static_cast<f32>(2.0 * EPSILON);
    CHECK(Distance(after.centre - before.centre, { .y = -risen }) < CLOSE);
    // One life later it is as far through its life, but born somewhere new.
    const gfx::SealParticle later = sparkAt(WRAP + EPSILON + ALL_LIGHT.sparkLife);
    CHECK(later.strength == doctest::Approx(after.strength).epsilon(CLOSE));
    CHECK(Distance(later.centre, after.centre) > CLOSE);
}

TEST_CASE("a stream whose laps pass 2^64 counts them modulo 2^32") {
    // A seal 2^-20 across whose shards drift 2^10 a second comes round every 2^-30 s; at 2^35 + 2^-10 s it has come round
    // 2^65 + 2^20 times, exactly, which no u64 holds: its lap is that count modulo 2^32, 2^20, and its age 0.
    constexpr f32 TINY = 1.0F / 1'048'576.0F;                // 2^-20
    constexpr f32 DRIFT = 1'024.0F;                          // 2^10
    constexpr f64 LATE = 34'359'738'368.0 + (1.0 / 1'024.0); // 2^35 + 2^-10
    constexpr u32 LAP = 1'048'576;                           // 2^20
    core::SealLook look = OneLayer();
    look.light = { .shards = 1, .shardDrift = DRIFT, .shardSize = LINE };
    const core::Seals looks = Table({ { "tiny", look } });
    const gfx::SealPack pack = gfx::PackSeals(looks, TestManifest());
    const std::array<core::Seal, 1> seals{ { { .key = core::SealKeyOf("tiny"), .point = POINT, .size = TINY } } };
    gfx::SealDrawing drawn;
    gfx::DrawSeals(seals, looks, pack, core::UiColours{}, LATE, static_cast<u64>(LATE * TICKS), true, drawn);
    REQUIRE(drawn.groundParticles.size() == 1);
    CHECK(drawn.groundParticles[0].shardLap == LAP);
    CHECK(drawn.groundParticles[0].shardAge == 0.0F);
}

TEST_CASE("a script part points at its sprite's field block") {
    constexpr f32 RADIUS = 0.9F;
    constexpr f32 GLYPH = 0.08F;
    constexpr u32 GLYPHS = 24;
    core::SealLook look = OneLayer();
    look.layers[0].parts = {
        { .kind = core::SealPartKind::Script, .radius = RADIUS, .size = GLYPH, .count = GLYPHS, .glyphSet = 0 }
    };
    core::Seals looks = Table({ { "script", look } });
    looks.glyphSets = { { SpriteId::Rune03, SpriteId::Rune04 } };
    const core::Manifest manifest = TestManifest();
    const gfx::SealPack pack = gfx::PackSeals(looks, manifest);
    REQUIRE(pack.parts.size() == 1);
    const gfx::SealGpuPart& part = pack.parts[0];
    REQUIRE((part.spriteCount == 2 && part.firstSprite < pack.sprites.size()));
    const gfx::SealGpuSprite& sprite = pack.sprites[part.firstSprite];
    const core::SpriteInfo& rune = manifest.Sprite(SpriteId::Rune03);
    const gfx::SpriteBlock block = gfx::FieldBlockOf(rune);
    const auto page = static_cast<f32>(manifest.pageSize);
    const core::Vec2 low{ .x = static_cast<f32>(block.x) / page, .y = static_cast<f32>(block.y) / page };
    const core::Vec2 high{ .x = static_cast<f32>(block.x + block.width) / page,
                           .y = static_cast<f32>(block.y + block.height) / page };
    CHECK(std::max(Distance(sprite.fieldMin, low), Distance(sprite.fieldMax, high)) < CLOSE_UV);
    CHECK(sprite.page == rune.page);
}

} // namespace
