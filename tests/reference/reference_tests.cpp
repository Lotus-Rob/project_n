// Renders fixed views offscreen and compares them with the committed pictures in tests/reference/
// (design doc 0002 §8.9). Run through `project_n reference`, which picks Mesa's software Vulkan and
// the working directory: each view's picture lands in build/reference/, where `--update` takes it from.
// Game text is also measured on the pictures (the phase 6 chunk 3 spec, "Testing"): a label's ink must not shimmer at
// sub-pixel camera offsets, nor step while its creature moves. So are the glows' drives (the phase 6 glow spec): a demo
// glow's drawn amount follows core::GlowStrength() from tick to tick, and reduce flashing holds and dims it.
#include "app/damage_numbers.hpp"
#include "app/hud.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/effects.hpp"
#include "core/frame.hpp"
#include "core/game_data.hpp"
#include "core/glow.hpp"
#include "core/instances.hpp"
#include "core/jobs.hpp"
#include "core/math.hpp"
#include "core/settings.hpp"
#include "core/strings.hpp"
#include "core/tick_clock.hpp"
#include "core/ui_colours.hpp"
#include "core/volley.hpp"
#include "data/strings.hpp"
#include "game/arena.hpp"
#include "game/combat.hpp"
#include "gfx/camera.hpp"
#include "gfx/image.hpp"
#include "gfx/renderer.hpp"
#include "gfx/sprite_block.hpp"
#include "gfx/text_layout.hpp"
#include "gfx/text_place.hpp"
#include "platform/clock.hpp"
#include "platform/paths.hpp"
#include "platform/window.hpp"
#include "support/manifest.hpp"

#include <doctest/doctest.h>

namespace {

// The shipped combat rules (assets/data/abilities.yaml's `combat:` section, which a data test checks).
constexpr core::CombatRules RULES{};

constexpr u32 WIDTH = 1280;
constexpr u32 HEIGHT = 720;
// Room for another CPU's rounding, not for a visible change: a missing orb must fail. Runs at other
// vector widths and thread counts, and from GCC and Clang, came out identical to the byte.
constexpr u8 TOLERANCE = 2;
constexpr std::size_t ALLOWED_PIXELS = std::size_t{ WIDTH } * HEIGHT / 10'000;

constexpr u64 TWO_SECONDS = 120; // of ticks: the foxes and orbs have moved off their starting places
constexpr f64 HALFWAY = 0.5;
// Between the tablet and the player, so both and their texts show at the largest zoom; at the others the whole ground does.
constexpr core::Vec2 TABLET_AND_PLAYER{ .x = 880.0F, .y = 690.0F };
// The foxes' orbit round the player in the crowd picture, in world units: close enough that their labels overlap.
constexpr f32 CROWD_ORBIT = 50.0F;
constexpr f32 CROWD_WOBBLE = 10.0F;
// Zoomed out as far as a label still reads on the pictures' 1280 by 720 (10 pixels: 20 world units × 2/3 × 0.75).
constexpr f32 CROWD_ZOOM = 0.75F;

struct View {
    std::string_view name;
    game::Scene scene = game::Scene::Arena;
    u64 ticks = 0;
    f64 alpha = 1.0;
    f32 zoom = 1.0F;
    bool outlines = false; // centred on the player's cultivator, hovered, with the first fox selected
    bool refit = false;    // a smaller picture first, so a scene target that isn't refitted shows
    bool crowd = false;    // the foxes bunched round the player's cultivator, centred on it
    std::optional<core::Vec2> centre = std::nullopt;
};

std::filesystem::path Assets() {
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    const std::string assetsWhy = assets.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(assets.has_value(), assetsWhy);
    return *assets;
}

// How long a scene's sprites may take to load: far past what a run takes, so a load that never finishes fails loudly
// instead of hanging the suite.
constexpr u64 SCENE_LOAD_LIMIT_NS = 60 * core::NS_PER_SECOND;

platform::Window OpenWindow() {
    std::expected<platform::Window, core::Error> window = platform::Window::Open("reference", WIDTH, HEIGHT);
    const std::string windowWhy = window.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(window.has_value(), windowWhy);
    return std::move(*window);
}

// A renderer for `window` holding `scene`'s sprites, its texts in English with outline and shadow.
gfx::Renderer RendererFor(platform::Window& window, core::SceneId scene, core::Jobs& jobs) {
    std::expected<core::Strings, core::Error> strings = data::LoadStrings(Assets() / "data");
    const std::string stringsWhy = strings.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(strings.has_value(), stringsWhy);
    std::expected<gfx::Renderer, core::Error> renderer = gfx::Renderer::Create(
        window,
        Assets(),
        test::TestManifest(),
        std::make_shared<const core::Strings>(std::move(*strings)),
        {},
        core::UiColours{}
    );
    const std::string rendererWhy = renderer.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(renderer.has_value(), rendererWhy);
    renderer->BeginScene(scene, jobs);
    std::expected<bool, core::Error> held = false;
    const u64 startedNs = platform::NowNs();
    while (held && !*held) {
        if (platform::NowNs() - startedNs > SCENE_LOAD_LIMIT_NS) {
            FAIL("the scene ", test::TestManifest()->Scene(scene).name, " wasn't resident within a minute");
        }
        held = renderer->ContinueScene(std::numeric_limits<u64>::max());
    }
    const std::string heldWhy = held.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(held.has_value(), heldWhy);
    return std::move(*renderer);
}

// What `arena` publishes at simulation tick `tick`.
core::Snapshot Published(game::Arena& arena, core::Jobs& jobs, u64 tick = 0) {
    core::Snapshot snapshot;
    snapshot.simulationTick = tick; // as core::Logic stamps it
    arena.Publish(snapshot, jobs);
    snapshot.scene = arena.Scene(); // as core::Logic names it
    return snapshot;
}

gfx::Image Captured(
    gfx::Renderer& renderer,
    const core::Snapshot& snapshot,
    const gfx::View& view,
    core::Jobs& jobs,
    u32 width = WIDTH,
    u32 height = HEIGHT,
    std::span<const core::TextItem> extra = {},
    const gfx::HudDraw& hud = {}
) {
    std::expected<gfx::Image, core::Error> drawn = renderer.Capture(snapshot, view, jobs, width, height, extra, hud);
    const std::string drawnWhy = drawn.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(drawn.has_value(), drawnWhy);
    return std::move(*drawn);
}

void CompareWithReference(const gfx::Image& drawn, std::string_view name) {
    const std::string file = std::format("{}.png", name);
    const std::expected<void, core::Error> saved = gfx::SavePng(drawn, std::filesystem::path{ "build/reference" } / file);
    const std::string savedWhy = saved.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(saved.has_value(), savedWhy);
    const std::expected<gfx::Image, core::Error> reference = gfx::LoadPng(std::filesystem::path{ "tests/reference" } / file);
    const std::string referenceWhy = reference.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(reference.has_value(), referenceWhy);
    const std::size_t differing = gfx::DifferingPixels(drawn, *reference, TOLERANCE);
    INFO("compare build/reference/", file, " with tests/reference/", file);
    CHECK(differing <= ALLOWED_PIXELS);
}

void CheckAgainstReference(const View& view) {
    core::math::SetFloatingPointEnvironment();
    core::Jobs jobs;
    core::GameData data = game::BuiltInData();
    if (view.crowd) {
        data.fox.orbitRadius = CROWD_ORBIT;
        data.fox.orbitWobble = CROWD_WOBBLE;
    }
    game::Arena arena(test::TestManifest(), view.scene, data);
    std::vector<core::Event> events; // the hits the ticks start, which no picture shows
    for (u64 tick = 1; tick <= view.ticks; ++tick) {
        arena.Tick({ .wall = tick, .simulation = tick, .state = tick }, {}, events);
    }
    if (view.outlines) {
        // The fox spawned first: the lowest ID among them.
        core::StableId fox{ std::numeric_limits<u64>::max() };
        const core::Registry& entities = arena.State()[game::ACTIVE].Entities();
        for (auto [entity, creature, stableId] : entities.view<const game::Creature, const core::StableId>().each()) {
            if (creature.kind == game::CreatureKind::Fox && std::to_underlying(stableId) < std::to_underlying(fox)) {
                fox = stableId;
            }
        }
        const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = fox } } };
        arena.Tick(
            { .wall = view.ticks + 1, .simulation = view.ticks + 1, .state = view.ticks + 1 },
            { .actions = select, .held = {} },
            events
        );
        REQUIRE(arena.Selected() == fox);
    }
    const core::Snapshot snapshot = Published(arena, jobs);

    platform::Window window = OpenWindow();
    gfx::Renderer renderer = RendererFor(window, snapshot.scene, jobs);
    gfx::View shown{ .alpha = view.alpha, .camera = { .zoom = view.zoom } };
    if (view.outlines) {
        const auto player = std::ranges::find(snapshot.ids, arena.Player());
        REQUIRE(player != snapshot.ids.end());
        shown.outlined = static_cast<std::size_t>(player - snapshot.ids.begin());
        shown.camera.centre = snapshot.follow.value_or(core::Follow{}).current;
    }
    if (view.crowd) {
        shown.camera.centre = snapshot.follow.value_or(core::Follow{}).current;
    }
    if (view.centre) {
        shown.camera.centre = *view.centre;
    }
    if (view.refit) {
        static_cast<void>(Captured(renderer, snapshot, shown, jobs, WIDTH / 2, HEIGHT / 3));
    }
    CompareWithReference(Captured(renderer, snapshot, shown, jobs), view.name);
}

TEST_CASE("the painted scene: sprites, shadows and overlapping glows, after a smaller picture") {
    CheckAgainstReference({ .name = "painted", .scene = game::Scene::Painted, .refit = true });
}

TEST_CASE("the map cells at zoom 1") {
    CheckAgainstReference({ .name = "cells", .scene = game::Scene::Cells });
}

TEST_CASE("the map cells at the smallest zoom: the low mips") {
    CheckAgainstReference({ .name = "cells_far", .scene = game::Scene::Cells, .zoom = gfx::MIN_ZOOM });
}

TEST_CASE("the arena halfway between two ticks: interpolation and foot-y order") {
    CheckAgainstReference({ .name = "arena_halfway", .scene = game::Scene::Arena, .ticks = TWO_SECONDS, .alpha = HALFWAY });
}

TEST_CASE("a fox dissolving, at half its progress") {
    constexpr u64 AURA_TICK = 60; // the aura's first hit, which kills every fox in its reach
    constexpr u64 SHOWN_AT = AURA_TICK + (RULES.dyingTicks / 2);
    constexpr core::Vec2 CRYSTAL_FOOT{ .x = 420.0F, .y = 620.0F };
    core::math::SetFloatingPointEnvironment();
    core::Jobs jobs;
    core::GameData data = game::BuiltInData();
    data.fox.health = 1;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, data);
    std::vector<core::Event> events;
    for (u64 tick = 1; tick <= SHOWN_AT; ++tick) {
        arena.Tick({ .wall = tick, .simulation = tick, .state = tick }, {}, events);
    }
    // The dying fox nearest the crystal, centred: the middle of its sprite, half its foot's height over its foot.
    const core::Registry& entities = arena.State()[game::ACTIVE].Entities();
    std::optional<core::Vec2> nearest;
    for (auto [entity, body, dying] : entities.view<const game::Body, const game::Dying>().each()) {
        const auto distance = [](core::Vec2 foot) { return std::hypot(foot.x - CRYSTAL_FOOT.x, foot.y - CRYSTAL_FOOT.y); };
        if (!nearest || distance(body.current) < distance(*nearest)) {
            nearest = body.current;
        }
    }
    REQUIRE(nearest.has_value());
    const f32 footY = static_cast<f32>(test::TestManifest()->Sprite(core::atlas::SpriteId::Fox).footY);
    const core::Vec2 centre = nearest.value_or(core::Vec2{}) - core::Vec2{ .x = 0.0F, .y = footY / 2.0F };
    const core::Snapshot snapshot = Published(arena, jobs, SHOWN_AT);
    platform::Window window = OpenWindow();
    gfx::Renderer renderer = RendererFor(window, snapshot.scene, jobs);
    const gfx::View view{ .camera = { .centre = centre, .zoom = gfx::MAX_ZOOM } };
    CompareWithReference(Captured(renderer, snapshot, view, jobs), "fox_dissolving");
}

// An ability cast at CAST_TICK among the foxes to the player's lower left, drawn some ticks later.
constexpr u64 CAST_TICK = 120;
constexpr core::Vec2 AMONG_THE_FOXES{ .x = 690.0F, .y = 700.0F };

// The arena with `slot`'s ability cast at AMONG_THE_FOXES on CAST_TICK, published `after` ticks later, with Qi enough
// for any cast.
core::Snapshot CastThenPublish(core::Slot slot, u64 after, core::Jobs& jobs) {
    // Well past any ability's end: a later picture is a broken computation, which fails here instead of ticking on.
    constexpr u64 MOST_TICKS_AFTER_CAST = 600;
    REQUIRE(after <= MOST_TICKS_AFTER_CAST);
    core::GameData data = game::BuiltInData();
    data.abilities.qiStart = data.abilities.qiMax;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, data);
    std::vector<core::Event> events;
    const std::array<core::Action, 1> cast{ { { .kind = core::ActionKind::Cast, .slot = slot, .point = AMONG_THE_FOXES } } };
    const u64 shownAt = CAST_TICK + after;
    for (u64 tick = 1; tick <= shownAt; ++tick) {
        const core::TickInput input = tick == CAST_TICK ? core::TickInput{ .actions = cast, .held = {} } : core::TickInput{};
        arena.Tick({ .wall = tick, .simulation = tick, .state = tick }, input, events);
    }
    return Published(arena, jobs, shownAt);
}

// CastThenPublish() of a volley: one in flight.
core::Snapshot CastAndWait(core::Slot slot, u64 after, core::Jobs& jobs) {
    core::Snapshot snapshot = CastThenPublish(slot, after, jobs);
    REQUIRE(snapshot.volleys.size() == 1);
    return snapshot;
}

void CheckVolley(std::string_view name, u64 after, const gfx::View& view = {}) {
    core::math::SetFloatingPointEnvironment();
    core::Jobs jobs;
    const core::Snapshot snapshot = CastAndWait(core::Slot::Special1, after, jobs);
    platform::Window window = OpenWindow();
    gfx::Renderer renderer = RendererFor(window, snapshot.scene, jobs);
    CompareWithReference(Captured(renderer, snapshot, view, jobs), name);
}

TEST_CASE("the flying swords mid-volley: the ring and the converging paths") {
    // A copy: the game data BuiltInData() returns is a temporary, gone at the end of this line.
    const core::VolleyShape swords = game::BuiltInData().abilities.Of(core::AbilityKind::FlyingSwords).volley;
    // The third ring halfway through fading in round the first two, which stand still, tip to heel (none leaves before
    // its appearing and hold are over): o then O. Then the third wave's middle swords halfway through their flight.
    constexpr u64 THIRD_WAVE = 2;
    const u64 appearing = (THIRD_WAVE * swords.waveGapTicks) + ((swords.staggerTicks + swords.outTicks) / 2);
    REQUIRE(appearing < swords.outTicks + swords.holdTicks);
    CheckVolley("volley_ring", appearing);
    const u64 leaving = (THIRD_WAVE * swords.waveGapTicks) + (swords.staggerTicks / 2) + swords.outTicks + swords.holdTicks +
                        (swords.departJitterTicks / 2);
    CheckVolley("volley_converging", leaving + (swords.flightTicks / 2));
}

TEST_CASE("the flying swords plunging, close up: the foxes they flew at died, so they stay where they fell and fade") {
    // A copy: the game data BuiltInData() returns is a temporary, gone at the end of this line.
    const core::VolleyShape swords = game::BuiltInData().abilities.Of(core::AbilityKind::FlyingSwords).volley;
    // The middle wave's middle swords arrived a few ticks ago, into foxes the first swords killed.
    constexpr u64 SINCE_ARRIVAL = 4;
    const u64 middleWave = swords.waves / 2;
    const u64 after = (middleWave * swords.waveGapTicks) + (swords.staggerTicks / 2) + swords.outTicks + swords.holdTicks +
                      (swords.departJitterTicks / 2) + swords.flightTicks + SINCE_ARRIVAL;
    {
        core::Jobs jobs;
        const core::Snapshot snapshot = CastAndWait(core::Slot::Special1, after, jobs);
        const auto dead = [](const core::VolleyTarget& target) { return target.diedTick != 0; };
        REQUIRE(std::ranges::all_of(snapshot.volleyTargets, dead));
    }
    CheckVolley("volley_plunge", after, { .camera = { .centre = AMONG_THE_FOXES, .zoom = gfx::MAX_ZOOM } });
}

TEST_CASE("a fire bolt in flight, close up: its burn's colour shows on the pale ground") {
    constexpr u64 IN_FLIGHT = 6; // of about a dozen ticks to the foxes
    core::math::SetFloatingPointEnvironment();
    core::Jobs jobs;
    const core::Snapshot snapshot = CastAndWait(core::Slot::Generator, IN_FLIGHT, jobs);
    const core::VolleyView& bolt = snapshot.volleys.front();
    REQUIRE(bolt.ability == core::AbilityKind::FireBolt);
    const std::span<const core::VolleyTarget> targets =
        std::span{ snapshot.volleyTargets }.subspan(bolt.firstTarget, bolt.targets);
    const core::Vec2 aim = core::SwordAim(bolt.volley, 0, snapshot.simulationTick, 1.0F, targets);
    const core::SwordPose pose = core::SwordAt(bolt.volley, 0, core::RenderTick(snapshot.simulationTick, 1.0), aim);
    REQUIRE(pose.shown > 0.0F);
    platform::Window window = OpenWindow();
    gfx::Renderer renderer = RendererFor(window, snapshot.scene, jobs);
    const gfx::View view{ .camera = { .centre = pose.position, .zoom = gfx::MAX_ZOOM } };
    CompareWithReference(Captured(renderer, snapshot, view, jobs), "bolt_burn");
}

// The poison lance cast this many ticks before the picture: its burst a third of the way through, its slot cooling.
constexpr u64 SINCE_LANCE = 4;

TEST_CASE("the poison lance's burst, close up: a flash at the point in the poison's colour") {
    core::math::SetFloatingPointEnvironment();
    core::Jobs jobs;
    const core::Snapshot snapshot = CastThenPublish(core::Slot::Spender, SINCE_LANCE, jobs);
    REQUIRE(snapshot.flashes.size() == 1);
    platform::Window window = OpenWindow();
    gfx::Renderer renderer = RendererFor(window, snapshot.scene, jobs);
    const gfx::View view{ .camera = { .centre = AMONG_THE_FOXES, .zoom = gfx::MAX_ZOOM } };
    CompareWithReference(Captured(renderer, snapshot, view, jobs), "lance_burst");
}

TEST_CASE("new effects change the next picture of the same snapshot, and the shipped ones bring it back") {
    constexpr f32 TIMES = 3.0F;
    core::math::SetFloatingPointEnvironment();
    core::Jobs jobs;
    const core::Snapshot snapshot = CastThenPublish(core::Slot::Spender, SINCE_LANCE, jobs);
    REQUIRE(snapshot.flashes.size() == 1);
    platform::Window window = OpenWindow();
    gfx::Renderer renderer = RendererFor(window, snapshot.scene, jobs);
    const gfx::View view{ .camera = { .centre = AMONG_THE_FOXES, .zoom = gfx::MAX_ZOOM } };
    const gfx::Image shipped = Captured(renderer, snapshot, view, jobs);
    core::Effects bigger;
    bigger.poisonLance.startScale *= TIMES;
    bigger.poisonLance.endScale *= TIMES;
    renderer.SetEffects(bigger);
    const gfx::Image changed = Captured(renderer, snapshot, view, jobs);
    CHECK(changed.rgba != shipped.rgba);
    renderer.SetEffects(core::Effects{});
    const gfx::Image restored = Captured(renderer, snapshot, view, jobs);
    CHECK(restored.rgba == shipped.rgba);

    // A glow tuned live while a volley flies (the debug data watch): a smaller sword aura shows in the next picture.
    constexpr u64 SINCE_SWORDS = 30; // half a second: the first rings stand and their first swords leave
    constexpr f32 SMALLER_AURA = 4.0F;
    const core::Snapshot swords = CastAndWait(core::Slot::Special1, SINCE_SWORDS, jobs);
    const gfx::Image auraShipped = Captured(renderer, swords, {}, jobs);
    core::Effects smaller;
    smaller.glows.at(std::to_underlying(core::GlowRole::FlyingSwords)).size = SMALLER_AURA;
    renderer.SetEffects(smaller);
    const gfx::Image auraChanged = Captured(renderer, swords, {}, jobs);
    CHECK(auraChanged.rgba != auraShipped.rgba);
    renderer.SetEffects(core::Effects{});
    const gfx::Image auraRestored = Captured(renderer, swords, {}, jobs);
    CHECK(auraRestored.rgba == auraShipped.rgba);
}

TEST_CASE("the HUD at zoom 1, one slot cooling down") {
    core::math::SetFloatingPointEnvironment();
    core::Jobs jobs;
    const core::Snapshot snapshot = CastThenPublish(core::Slot::Spender, SINCE_LANCE, jobs);
    REQUIRE(snapshot.caster.has_value());
    std::expected<core::Strings, core::Error> strings = data::LoadStrings(Assets() / "data");
    const std::string stringsWhy = strings.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(strings.has_value(), stringsWhy);
    app::Hud hud(std::make_shared<const core::Strings>(std::move(*strings)), {}, core::UiColours{}); // parentheses: MSVC C4868
    std::vector<gfx::HudLine> lines;
    std::vector<gfx::HudBar> bars;
    hud.Build(
        snapshot.caster.value_or(core::CasterView{}), core::RenderTick(snapshot.simulationTick, 1.0), HEIGHT, lines, bars
    );
    platform::Window window = OpenWindow();
    gfx::Renderer renderer = RendererFor(window, snapshot.scene, jobs);
    const gfx::Image drawn = Captured(renderer, snapshot, {}, jobs, WIDTH, HEIGHT, {}, { .lines = lines, .bars = bars });
    CompareWithReference(drawn, "hud_zoom_one");
}

TEST_CASE("the hovered entity's pale outline and the selected one's gold, close up") {
    CheckAgainstReference({ .name = "outlines", .scene = game::Scene::Arena, .zoom = gfx::MAX_ZOOM, .outlines = true });
}

// The arena at tick GLOW_TICK, where the shrine, the stele and the ice crystal wear the demo glows (game::DemoGlow()), its
// snapshot stamped with that tick as core::Logic stamps it, shown at `zoom` round `centre`. Each glow is near its strongest
// there: the pulse an eighth of a beat past its top (0.87), the charge just full and held (1), the progress showing its
// second loop's result (1).
void CheckGlows(std::string_view name, f32 zoom, core::Vec2 centre) {
    constexpr u64 GLOW_TICK = 492;
    core::math::SetFloatingPointEnvironment();
    core::Jobs jobs;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, game::BuiltInData());
    std::vector<core::Event> events;
    for (u64 tick = 1; tick <= GLOW_TICK; ++tick) {
        arena.Tick({ .wall = tick, .simulation = tick, .state = tick }, {}, events);
    }
    const core::Snapshot snapshot = Published(arena, jobs, GLOW_TICK);
    platform::Window window = OpenWindow();
    gfx::Renderer renderer = RendererFor(window, snapshot.scene, jobs);
    const gfx::View view{ .camera = { .centre = centre, .zoom = zoom } };
    CompareWithReference(Captured(renderer, snapshot, view, jobs), name);
}

TEST_CASE("the demo glows at zoom 1") {
    // Among the shrine (its foot at 960, 330), the stele (760, 980) and the crystal (420, 620): all three in view.
    constexpr core::Vec2 AMONG_THE_THREE{ .x = 720.0F, .y = 580.0F };
    CheckGlows("glows_zoom_one", 1.0F, AMONG_THE_THREE);
}

TEST_CASE("at zoom 2 the glows thin") {
    // Between the crystal and the stele, both in view.
    constexpr core::Vec2 CRYSTAL_AND_STELE{ .x = 590.0F, .y = 740.0F };
    CheckGlows("glows_zoom_two", gfx::MAX_ZOOM, CRYSTAL_AND_STELE);
}

TEST_CASE("at the smallest zoom the glows stop at the field's reach") {
    CheckGlows("glows_zoom_quarter", gfx::MIN_ZOOM, gfx::REFERENCE_CENTRE);
}

TEST_CASE("at the smallest zoom the labels and the tablet's caption, too small to read, are faded out") {
    CheckAgainstReference({ .name = "text_zoom_quarter", .zoom = gfx::MIN_ZOOM });
}

TEST_CASE("at zoom one half the labels, too small to read, are faded out and the tablet's caption shows") {
    constexpr f32 HALF_ZOOM = 0.5F;
    CheckAgainstReference({ .name = "text_zoom_half", .zoom = HALF_ZOOM });
}

TEST_CASE("labels and the tablet's caption at zoom 1") {
    CheckAgainstReference({ .name = "text_zoom_one", .zoom = 1.0F });
}

TEST_CASE("crowded labels: the foxes' names stack into one, the player's shows") {
    CheckAgainstReference({ .name = "labels_crowd", .zoom = CROWD_ZOOM, .crowd = true });
}

TEST_CASE("labels and the tablet's caption at the largest zoom") {
    CheckAgainstReference({ .name = "text_zoom_two", .zoom = gfx::MAX_ZOOM, .centre = TABLET_AND_PLAYER });
}

// Damage numbers in a grid over the arena at rest, as app::DamageNumbers plays them at NUMBERS_TICK: a column per
// kind, a row per motion and age since the hit (0.1 and 0.5 s, and 0.8 s, fading), less each number's stagger delay
// (under NUMBER_STAGGER_MIN_SECONDS: each hits its own target, alone in its tick).
constexpr u64 NUMBERS_TICK = 600;
constexpr std::array<u64, 3> NUMBER_AGES{ { 6, 30, 48 } }; // in ticks
constexpr std::array<i32, core::HIT_KINDS> NUMBER_VALUES{ { 7, 23, 58, 140 } };
constexpr u64 NUMBER_TARGETS = 1'000'000; // the first of the grid's targets: no entity in the arena
// The grid's top-left number and the space between numbers, in world units: apart at the smallest zoom too.
constexpr core::Vec2 NUMBERS_ORIGIN{ .x = 600.0F, .y = 160.0F };
constexpr core::Vec2 NUMBERS_SPACING{ .x = 240.0F, .y = 160.0F };

// With `bare`, the outline and the shadow are off, and the numbers take their dark colours.
std::vector<core::TextItem> NumberGrid(bool bare, const core::UiColours& colours = {}) {
    std::vector<core::TextItem> texts;
    f32 row = 0.0F;
    for (const core::NumberMotion motion : { core::NumberMotion::Bounce, core::NumberMotion::Rise }) {
        app::DamageNumbers numbers(
            { .damageNumberMotion = motion, .textOutline = !bare, .textShadow = !bare, .uiColours = colours }
        );
        for (const u64 age : NUMBER_AGES) {
            for (std::size_t kind = 0; kind < core::HIT_KINDS; ++kind) {
                // Each kind on a target of its own, alone in its tick, so each stagger window is the narrowest.
                numbers.Play(
                    { .tick = NUMBERS_TICK - age,
                      .hit = static_cast<core::HitKind>(kind),
                      .target = core::StableId{ NUMBER_TARGETS + kind },
                      .position = NUMBERS_ORIGIN +
                                  core::Vec2{ .x = NUMBERS_SPACING.x * static_cast<f32>(kind), .y = NUMBERS_SPACING.y * row },
                      .value = NUMBER_VALUES.at(kind) }
                );
            }
            ++row;
        }
        numbers.Texts(static_cast<f64>(NUMBERS_TICK), texts);
    }
    return texts;
}

// The grid of numbers over the arena at rest, at `zoom`; with `bare`, the outline and the shadow off.
void CheckNumbers(std::string_view name, f32 zoom, bool bare = false) {
    core::math::SetFloatingPointEnvironment();
    core::Jobs jobs;
    game::Arena arena(test::TestManifest());
    const core::Snapshot snapshot = Published(arena, jobs);
    platform::Window window = OpenWindow();
    gfx::Renderer renderer = RendererFor(window, snapshot.scene, jobs);
    if (bare) {
        renderer.SetTextOptions({ .outline = false, .shadow = false });
    }
    const std::vector<core::TextItem> numbers = NumberGrid(bare);
    const gfx::View view{ .camera = { .zoom = zoom } };
    CompareWithReference(Captured(renderer, snapshot, view, jobs, WIDTH, HEIGHT, numbers), name);
}

TEST_CASE("damage numbers of every kind, bouncing and rising, at zoom 1") {
    CheckNumbers("numbers_one", 1.0F);
}

TEST_CASE("damage numbers at the smallest zoom keep their minimum size") {
    CheckNumbers("numbers_quarter", gfx::MIN_ZOOM);
}

TEST_CASE("with the outline and the shadow off, damage numbers turn dark, as labels do") {
    CheckNumbers("numbers_bare", 1.0F, /*bare=*/true);
}

// A table of one distinct, garish colour per entry: six saturated hues, each a little dimmer than the one 6 before it.
core::UiColours GarishColours() {
    constexpr std::array<std::array<u8, 3>, 6> HUES{
        { { 1, 0, 1 }, { 0, 1, 0 }, { 1, 1, 0 }, { 0, 1, 1 }, { 1, 0, 0 }, { 0, 0, 1 } }
    };
    constexpr u8 FULL = 255;
    constexpr u8 STEP = 30;
    core::UiColours colours;
    for (std::size_t colour = 0; colour < core::UI_COLOURS; ++colour) {
        const std::array<u8, 3>& hue = HUES.at(colour % HUES.size());
        const auto level = static_cast<u8>(FULL - (STEP * (colour / HUES.size())));
        colours.colours.at(colour) = core::FromSrgb(
            static_cast<u8>(hue[0] * level), static_cast<u8>(hue[1] * level), static_cast<u8>(hue[2] * level), FULL
        );
    }
    return colours;
}

// The arena at rest with the first fox selected and the player hovered, in the garish table, and the numbers grid; with
// `bare`, the outline and the shadow are off, so labels, the caption and the numbers take the table's ink and dark colours.
void CheckRecoloured(std::string_view name, bool bare) {
    core::math::SetFloatingPointEnvironment();
    core::Jobs jobs;
    game::Arena arena(test::TestManifest());
    std::vector<core::Event> events;
    // The fox spawned first: the lowest ID among them, as the outlines picture selects.
    core::StableId fox{ std::numeric_limits<u64>::max() };
    const core::Registry& entities = arena.State()[game::ACTIVE].Entities();
    for (auto [entity, creature, stableId] : entities.view<const game::Creature, const core::StableId>().each()) {
        if (creature.kind == game::CreatureKind::Fox && std::to_underlying(stableId) < std::to_underlying(fox)) {
            fox = stableId;
        }
    }
    const std::array<core::Action, 1> select{ { { .kind = core::ActionKind::Select, .target = fox } } };
    arena.Tick({ .wall = 1, .simulation = 1, .state = 1 }, { .actions = select, .held = {} }, events);
    REQUIRE(arena.Selected() == fox);
    const core::Snapshot snapshot = Published(arena, jobs);
    platform::Window window = OpenWindow();
    gfx::Renderer renderer = RendererFor(window, snapshot.scene, jobs);
    const core::UiColours garish = GarishColours();
    renderer.SetUiColours(garish);
    if (bare) {
        renderer.SetTextOptions({ .outline = false, .shadow = false });
    }
    const auto player = std::ranges::find(snapshot.ids, arena.Player());
    REQUIRE(player != snapshot.ids.end());
    const gfx::View view{ .outlined = static_cast<std::size_t>(player - snapshot.ids.begin()) };
    const std::vector<core::TextItem> numbers = NumberGrid(bare, garish);
    CompareWithReference(Captured(renderer, snapshot, view, jobs, WIDTH, HEIGHT, numbers), name);
}

TEST_CASE("labels, the caption, outlines and numbers in a recoloured table") {
    CheckRecoloured("recoloured", false);
}

TEST_CASE("with the outline and the shadow off, a recoloured table's ink and dark number colours show") {
    CheckRecoloured("recoloured_bare", true);
}

TEST_CASE("a number on a creature too small to see starts from its label") {
    // Two foxes' numbers, both anchored far from them on bare ground: the one whose label says its fox is
    // under gfx::VISIBLE_CREATURE_PIXELS stands on that label instead; the other stays at its own anchor.
    constexpr f32 TINY = 6.0F;                                   // world units: 4 pixels at zoom 1 on the 1280-wide picture
    constexpr core::Vec2 FAR_ANCHOR{ .x = 330.0F, .y = 900.0F }; // on bare ground, bottom left
    constexpr core::Vec2 NEXT_TO_IT{ .x = 120.0F, .y = 0.0F };
    constexpr i32 ON_TINY = 77;
    constexpr i32 ON_SEEN = 33;
    core::math::SetFloatingPointEnvironment();
    core::Jobs jobs;
    game::Arena arena(test::TestManifest());
    core::Snapshot snapshot = Published(arena, jobs);
    std::vector<core::TextItem*> foxes;
    for (core::TextItem& text : snapshot.texts) {
        if (text.kind == core::TextKind::Label && text.priority == core::LabelPriority::Rest) {
            foxes.push_back(&text);
        }
    }
    REQUIRE(foxes.size() >= 2);
    foxes[0]->height = TINY;
    REQUIRE(TINY * gfx::PixelsPerUnit({}, WIDTH, HEIGHT) < gfx::VISIBLE_CREATURE_PIXELS);
    const std::array<core::TextItem, 2> numbers{ {
        { .kind = core::TextKind::Number,
          .value = ON_TINY,
          .previous = FAR_ANCHOR,
          .current = FAR_ANCHOR,
          .entity = foxes[0]->entity },
        { .kind = core::TextKind::Number,
          .value = ON_SEEN,
          .previous = FAR_ANCHOR + NEXT_TO_IT,
          .current = FAR_ANCHOR + NEXT_TO_IT,
          .entity = foxes[1]->entity },
    } };
    platform::Window window = OpenWindow();
    gfx::Renderer renderer = RendererFor(window, snapshot.scene, jobs);
    CompareWithReference(Captured(renderer, snapshot, {}, jobs, WIDTH, HEIGHT, numbers), "numbers_label");
}

// What a label is measured over: the backdrop's flat dark, where its light fill is most of its ink, or a flat pale
// ground like the painted paper, where its dark outline and shadow are.
enum class Ground : u8 {
    Backdrop,
    Pale,
};
constexpr std::array<Ground, 2> GROUNDS{ { Ground::Backdrop, Ground::Pale } };

std::string_view Over(Ground ground) {
    return ground == Ground::Pale ? "over the pale ground" : "over the backdrop";
}

// The player's label in a snapshot of the arena at rest, the label's measures and the snapshot without it, so a
// picture's difference from the other is that label's ink alone. Both leave out the arena's sprites: over one flat
// colour, the ink is the text's own, not the ground's grass resampled at another phase or an orb passing under it.
struct LabelScene {
    core::Snapshot with{};
    core::Snapshot without{};
    core::TextItem label{};
    gfx::Layout layout{};
};

// The manifest's arena scene, whose sprites the label measurements' renderer holds.
core::SceneId ArenaScene() {
    const std::optional<core::SceneId> arena = test::TestManifest()->SceneNamed(game::SceneName(game::Scene::Arena));
    REQUIRE(arena.has_value());
    return arena.value_or(core::SceneId{});
}

// The painted ground with its region shrunk to one texel at its middle: the whole ground in one flat pale colour.
core::Instance FlatPaleGround() {
    constexpr u32 HALF_BITS = 16;
    constexpr u32 LOW_HALF = 0xFFFFU;
    core::Instance ground = core::MakeInstance(*test::TestManifest(), { .sprite = core::atlas::SpriteId::Ground });
    const auto middle = [&](u32 shift) {
        return static_cast<u16>((((ground.regionMin >> shift) & LOW_HALF) + ((ground.regionMax >> shift) & LOW_HALF)) / 2);
    };
    ground.regionMin = core::PackPair(middle(0), middle(HALF_BITS));
    ground.regionMax = ground.regionMin;
    return ground;
}

LabelScene PlayerLabel(core::Jobs& jobs, Ground ground) {
    game::Arena arena(test::TestManifest());
    LabelScene scene{ .with = Published(arena, jobs) };
    const auto label = std::ranges::find(scene.with.texts, core::LabelPriority::Player, &core::TextItem::priority);
    REQUIRE(label != scene.with.texts.end());
    scene.label = *label;
    scene.with.instances.clear();
    scene.with.batches.clear();
    scene.with.ids.clear();
    if (ground == Ground::Pale) {
        scene.with.instances.push_back(FlatPaleGround());
        scene.with.batches.push_back({ .first = 0, .count = 1, .pipeline = core::Pipeline::Normal });
        scene.with.ids.emplace_back();
    }
    scene.with.wallTick = u64{ std::to_underlying(ground) } + 1U; // each ground's instances are uploaded
    scene.without = scene.with;
    scene.without.texts.erase(scene.without.texts.begin() + (label - scene.with.texts.begin()));
    std::expected<gfx::TextLayout, core::Error> layout = gfx::TextLayout::Open(Assets() / "fonts");
    const std::string layoutWhy = layout.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(layout.has_value(), layoutWhy);
    std::expected<core::Strings, core::Error> strings = data::LoadStrings(Assets() / "data");
    const std::string stringsWhy = strings.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(strings.has_value(), stringsWhy);
    scene.layout = layout->Lay(strings->Text(scene.label.string, core::Language::English), scene.label.direction);
    return scene;
}

// The ink a label adds: its pixels' darkness (1 − luminance) less the picture's without it, summed. Signed, as light
// adds: a pixel half fill and half outline counts both, where an absolute difference would cancel them.
struct Ink {
    f64 total = 0.0;   // its size, whichever way it goes (a light label on the dark backdrop lightens)
    f64 centreX = 0.0; // the ink-weighted mean x, in pixels
};

// A pixel's red, green and blue, linear, as the scene is blended: what a pixel gains then grows in proportion to how much
// of it a glyph or a glow covers.
std::array<f64, 3> LinearAt(const gfx::Image& image, std::size_t pixel) {
    constexpr f64 LEVELS = 255.0;
    constexpr f64 KNEE = 0.04045;
    constexpr f64 SLOPE = 12.92;
    constexpr f64 SHIFT = 0.055;
    constexpr f64 GAMMA = 2.4;
    const std::size_t first = pixel * gfx::BYTES_PER_TEXEL;
    std::array<f64, 3> linear{};
    for (std::size_t channel = 0; channel < linear.size(); ++channel) {
        const f64 encoded = static_cast<f64>(std::to_integer<u8>(image.rgba.at(first + channel))) / LEVELS;
        linear.at(channel) = encoded <= KNEE ? encoded / SLOPE : std::pow((encoded + SHIFT) / (1.0 + SHIFT), GAMMA);
    }
    return linear;
}

f64 Luminance(const gfx::Image& image, std::size_t pixel) {
    constexpr f64 RED = 0.2126;
    constexpr f64 GREEN = 0.7152;
    constexpr f64 BLUE = 0.0722;
    const std::array<f64, 3> linear = LinearAt(image, pixel);
    return (RED * linear[0]) + (GREEN * linear[1]) + (BLUE * linear[2]);
}

// The label's ink between two pictures, inside the label's box as the renderer places it for `view`: the box holds
// its glyphs' fields whole, with the outline and the shadow.
Ink InkOf(const LabelScene& scene, const gfx::View& view, const gfx::Image& with, const gfx::Image& without) {
    const f32 pixels = gfx::TextPixels(scene.label.kind, gfx::PixelsPerUnit(view.camera, WIDTH, HEIGHT), 1.0F);
    const f32 scale = pixels / gfx::SDF_BASE_PIXELS;
    const core::Vec2 size{ .x = scene.layout.width * scale, .y = scene.layout.height * scale };
    const core::Vec2 topLeft = gfx::TextTopLeft(gfx::TextAnchor(scene.label, view, WIDTH, HEIGHT), size, scene.label.align);
    const auto left = static_cast<u32>(std::floor(topLeft.x));
    const auto top = static_cast<u32>(std::floor(topLeft.y));
    const auto right = static_cast<u32>(std::ceil(topLeft.x + size.x));
    const auto bottom = static_cast<u32>(std::ceil(topLeft.y + size.y));
    REQUIRE(right < WIDTH);
    REQUIRE(bottom < HEIGHT);
    f64 sum = 0.0;
    f64 weightedX = 0.0;
    for (u32 row = top; row <= bottom; ++row) {
        for (u32 column = left; column <= right; ++column) {
            const std::size_t pixel = (std::size_t{ row } * WIDTH) + column;
            const f64 added = Luminance(without, pixel) - Luminance(with, pixel);
            sum += added;
            constexpr f64 PIXEL_CENTRE = 0.5;
            weightedX += added * (static_cast<f64>(column) + PIXEL_CENTRE);
        }
    }
    REQUIRE(sum != 0.0);
    return { .total = std::abs(sum), .centreX = weightedX / sum };
}

TEST_CASE("a label's ink is the same at sub-pixel camera offsets, over a dark and a pale ground") {
    constexpr std::array<f32, 3> OFFSETS{ { 0.0F, 1.0F / 3.0F, 2.0F / 3.0F } }; // in pixels
    constexpr f64 MOST_CHANGE = 0.02;
    core::math::SetFloatingPointEnvironment();
    core::Jobs jobs;
    platform::Window window = OpenWindow();
    gfx::Renderer renderer = RendererFor(window, ArenaScene(), jobs);
    for (const Ground ground : GROUNDS) {
        const LabelScene scene = PlayerLabel(jobs, ground);
        const gfx::Camera centred{ .centre = scene.label.current, .zoom = 1.0F };
        const f32 worldPerPixel = 1.0F / gfx::PixelsPerUnit(centred, WIDTH, HEIGHT);
        std::vector<f64> totals;
        for (const f32 offsetX : OFFSETS) {
            for (const f32 offsetY : OFFSETS) {
                gfx::View view{ .camera = centred };
                view.camera.centre = view.camera.centre + (core::Vec2{ .x = offsetX, .y = offsetY } * worldPerPixel);
                const gfx::Image with = Captured(renderer, scene.with, view, jobs);
                const gfx::Image without = Captured(renderer, scene.without, view, jobs);
                totals.push_back(InkOf(scene, view, with, without).total);
            }
        }
        const auto [least, most] = std::ranges::minmax(totals);
        INFO(Over(ground), ", the label's ink from ", least, " to ", most);
        CHECK((most / least) - 1.0 < MOST_CHANGE);
    }
}

TEST_CASE("a label following its creature doesn't step, over a dark and a pale ground") {
    constexpr std::array<f64, 5> ALPHAS{ { 0.0, 0.25, 0.5, 0.75, 1.0 } };
    constexpr f32 MOVED = 8.0F; // world units in x between the two ticks
    constexpr f64 MOST_ERROR = 0.15;
    core::math::SetFloatingPointEnvironment();
    core::Jobs jobs;
    platform::Window window = OpenWindow();
    gfx::Renderer renderer = RendererFor(window, ArenaScene(), jobs);
    for (const Ground ground : GROUNDS) {
        LabelScene scene = PlayerLabel(jobs, ground);
        scene.label.previous = scene.label.current - core::Vec2{ .x = MOVED, .y = 0.0F };
        std::ranges::find(scene.with.texts, core::LabelPriority::Player, &core::TextItem::priority)->previous =
            scene.label.previous;
        const gfx::Camera camera{ .centre = scene.label.current, .zoom = 1.0F };
        // A step's pixels: not a whole number here, so a label snapped to whole pixels would step unevenly.
        const f64 step = static_cast<f64>(MOVED) / static_cast<f64>(ALPHAS.size() - 1) *
                         static_cast<f64>(gfx::PixelsPerUnit(camera, WIDTH, HEIGHT));
        std::vector<f64> centres;
        for (const f64 alpha : ALPHAS) {
            const gfx::View view{ .alpha = alpha, .camera = camera };
            const gfx::Image with = Captured(renderer, scene.with, view, jobs);
            const gfx::Image without = Captured(renderer, scene.without, view, jobs);
            centres.push_back(InkOf(scene, view, with, without).centreX);
        }
        for (std::size_t index = 1; index < centres.size(); ++index) {
            INFO(
                Over(ground), ", from alpha ", ALPHAS.at(index - 1), " to ", ALPHAS.at(index), ", expecting ", step, " pixels"
            );
            CHECK(std::abs(centres.at(index) - centres.at(index - 1) - step) < MOST_ERROR);
        }
    }
}

// A demo glow drawn alone, to be measured (the phase 6 glow spec, "Testing"): its prop at simulation `tick`, the render
// clock `alpha` of the way from the tick before, with nothing else but, if `ground`, the pale ground (else the dark
// backdrop, where a light glow adds without saturating).
struct GlowShot {
    core::GlowRole role = core::GlowRole::None;
    u64 tick = 0;
    f64 alpha = 1.0;
    bool ground = true;
    // A start tick in place of the demo's, as core::MakeInstance keeps it: its low 32 bits.
    std::optional<u64> start = std::nullopt;
};

// The prop wearing `shot`'s glow as the arena publishes it at the shot's tick, its start tick replaced if the shot gives one.
core::Instance GlowingProp(game::Arena& arena, core::Jobs& jobs, const GlowShot& shot) {
    constexpr u32 ROLE_BITS = 0xFFU;
    const core::Snapshot published = Published(arena, jobs, shot.tick);
    const auto prop = std::ranges::find_if(published.instances, [&](const core::Instance& instance) {
        return (instance.glow & ROLE_BITS) == std::to_underlying(shot.role);
    });
    REQUIRE(prop != published.instances.end());
    core::Instance found = *prop;
    if (shot.start) {
        found.glowStart = static_cast<u32>(*shot.start);
    }
    return found;
}

// A snapshot at `tick` of `instances` alone, in one batch; `wallTick` tells it apart, so the renderer uploads it.
core::Snapshot SnapshotOf(std::span<const core::Instance> instances, u64 tick, u64 wallTick) {
    core::Snapshot snapshot{ .wallTick = wallTick, .simulationTick = tick, .scene = ArenaScene() };
    snapshot.instances.assign(instances.begin(), instances.end());
    snapshot.ids.resize(instances.size());
    snapshot.batches.push_back({ .first = 0, .count = static_cast<u32>(instances.size()), .pipeline = core::Pipeline::Normal });
    return snapshot;
}

// A packed colour's channels from 0 to 1, as the shader unpacks them (linear, premultiplied).
std::array<f64, 4> Unpacked(u32 packed) {
    constexpr u32 BYTE = 0xFFU;
    constexpr u32 BITS_PER_CHANNEL = 8;
    constexpr f64 LEVELS = 255.0;
    std::array<f64, 4> channels{};
    for (std::size_t channel = 0; channel < channels.size(); ++channel) {
        channels.at(channel) = static_cast<f64>((packed >> (BITS_PER_CHANNEL * channel)) & BYTE) / LEVELS;
    }
    return channels;
}

// What a glow measurement draws with: the renderer, the arena whose props wear the demo glows, the effects the renderer
// holds, and a count of the snapshots shown, which tells each new one apart.
struct GlowRig {
    gfx::Renderer* renderer = nullptr;
    game::Arena* arena = nullptr;
    core::Jobs* jobs = nullptr;
    core::Effects effects{};
    u64 uploads = 0;
};

// How much glow `shot` draws: summed over the picture, each pixel's change from the prop without its glow, as a fraction
// of the most the glow's colour can change it there (ink takes what is under it towards the colour, light adds the
// colour). That is the glow's coverage times its drawn opacity, summed: the same shape at two strengths gives their ratio,
// whatever the colour.
f64 GlowAmount(GlowRig& rig, const GlowShot& shot) {
    const core::GlowLook& look = rig.effects.glows.at(std::to_underlying(shot.role));
    const core::GlowInput input = game::DemoGlow(shot.role, shot.tick);
    // As the shader picks it: a result's colour, else the look's, which may be the element's effect colour.
    const core::UiColour own = look.colour.element ? core::EffectColour(input.element) : look.colour.colour;
    const core::UiColour colour = input.result == 0 ? own : look.drive.results.at(std::size_t{ input.result } - 1);
    const std::array<f64, 4> tint = Unpacked(core::UiColours{}.Of(colour));
    const core::Instance prop = GlowingProp(*rig.arena, *rig.jobs, shot);
    core::Instance plain = prop;
    plain.glow = 0;
    std::vector<core::Instance> under;
    if (shot.ground) {
        under.push_back(core::MakeInstance(*test::TestManifest(), { .sprite = core::atlas::SpriteId::Ground }));
    }
    const gfx::View view{ .alpha = shot.alpha, .camera = { .centre = prop.current, .zoom = 1.0F } };
    const auto capture = [&](std::optional<core::Instance> top) {
        std::vector<core::Instance> drawn = under;
        if (top) {
            drawn.push_back(*top);
        }
        return Captured(*rig.renderer, SnapshotOf(drawn, shot.tick, ++rig.uploads), view, *rig.jobs);
    };
    const gfx::Image glowing = capture(prop);
    const gfx::Image bare = capture(plain);
    const gfx::Image beneath = capture(std::nullopt);
    constexpr std::size_t ALPHA = 3;
    f64 amount = 0.0;
    for (std::size_t pixel = 0; pixel < std::size_t{ WIDTH } * HEIGHT; ++pixel) {
        const std::array<f64, 3> with = LinearAt(glowing, pixel);
        const std::array<f64, 3> without = LinearAt(bare, pixel);
        const std::array<f64, 3> background = LinearAt(beneath, pixel);
        f64 along = 0.0;
        f64 most = 0.0;
        for (std::size_t channel = 0; channel < with.size(); ++channel) {
            const f64 full = look.blend == core::GlowBlend::Ink ? tint.at(channel) - (tint.at(ALPHA) * background.at(channel))
                                                                : tint.at(channel);
            along += (with.at(channel) - without.at(channel)) * full;
            most += full * full;
        }
        if (most > 0.0) {
            amount += along / most;
        }
    }
    return amount;
}

// The glow `shot` should draw, up to its shape (GlowAmount() sums it over the shape): core::GlowStrength() of the demo's
// inputs at the shot's render time (its value between the two ticks, its age from the start, negative before it), times
// the look's opacity, which reduce flashing scales for light.
f64 ExpectedGlow(const core::Effects& effects, const GlowShot& shot, bool reduceFlashing) {
    const core::GlowLook& look = effects.glows.at(std::to_underlying(shot.role));
    const core::GlowInput input = game::DemoGlow(shot.role, shot.tick);
    const f64 age = core::RenderTick(shot.tick, shot.alpha) - static_cast<f64>(shot.start.value_or(input.startTick));
    const auto value =
        static_cast<f32>(std::lerp(static_cast<f64>(input.previousValue), static_cast<f64>(input.currentValue), shot.alpha));
    const f32 strength = core::GlowStrength(look.drive, value, age, input.result, input.released, reduceFlashing);
    const f32 dimmed = reduceFlashing && look.blend == core::GlowBlend::Light ? core::REDUCED_LIGHT_OPACITY : 1.0F;
    return static_cast<f64>(strength * look.opacity * dimmed);
}

// Two shots of one glow, whose drawn amounts' ratio is checked against GlowStrength()'s.
struct GlowPair {
    std::string_view why;
    GlowShot first{};
    GlowShot second{};
};

TEST_CASE("each drive's drawn glow follows GlowStrength, also with start ticks across 2^32 and ahead of the clock") {
    constexpr f64 MOST_ERROR = 0.05;
    constexpr u64 PAST_32_BITS = u64{ 1 } << 32U;
    // Just short of 2^32: the pulse's age crosses the wrap of the low 32 bits between the two shots.
    constexpr u64 LATE_START = PAST_32_BITS - 10;
    constexpr u64 HALF_BEAT = 48; // 0.8 s: the pulse at its lowest
    constexpr u64 HELD = 120;
    constexpr u64 HALF_FADED = 156;
    constexpr u64 HALF_FILLED = 90;
    constexpr u64 WITH_RESULT = 200;
    constexpr u64 RELEASED = 150; // released at 144 when full: its release moved to this tick
    using enum core::GlowRole;
    const std::array<GlowPair, 5> pairs{ {
        { .why = "the pulse at its beat's start and half",
          .first = { .role = DemoPulse, .tick = 0 },
          .second = { .role = DemoPulse, .tick = HALF_BEAT } },
        { .why = "the charge held and half faded",
          .first = { .role = DemoCharge, .tick = HELD },
          .second = { .role = DemoCharge, .tick = HALF_FADED } },
        { .why = "the progress at one half and with a result",
          .first = { .role = DemoProgress, .tick = HALF_FILLED },
          .second = { .role = DemoProgress, .tick = WITH_RESULT } },
        { .why = "the pulse at its beat's start and half, its start tick near 2^32",
          .first = { .role = DemoPulse, .tick = LATE_START, .start = LATE_START },
          .second = { .role = DemoPulse, .tick = LATE_START + HALF_BEAT, .start = LATE_START } },
        // The render clock half a tick short of the release: the age is -0.5, the wrapping difference read signed, and the
        // charge hasn't begun to fade (read unsigned, the age would be 2^32 and the glow gone).
        { .why = "the charge released a tick ahead of the clock's whole tick, and half faded",
          .first = { .role = DemoCharge, .tick = RELEASED, .alpha = HALFWAY, .start = RELEASED },
          .second = { .role = DemoCharge, .tick = HALF_FADED } },
    } };
    core::math::SetFloatingPointEnvironment();
    core::Jobs jobs;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, game::BuiltInData());
    platform::Window window = OpenWindow();
    gfx::Renderer renderer = RendererFor(window, ArenaScene(), jobs);
    GlowRig rig{ .renderer = &renderer, .arena = &arena, .jobs = &jobs };
    for (const GlowPair& pair : pairs) {
        const f64 drawn = GlowAmount(rig, pair.first) / GlowAmount(rig, pair.second);
        const f64 expected = ExpectedGlow(rig.effects, pair.first, false) / ExpectedGlow(rig.effects, pair.second, false);
        INFO(pair.why, ": drawn ", drawn, " to 1, GlowStrength ", expected, " to 1");
        CHECK(std::abs((drawn / expected) - 1.0) < MOST_ERROR);
    }
}

TEST_CASE("reduce flashing holds a pulse at its average and dims light glows, and leaves other ink glows alone") {
    constexpr f64 MOST_ERROR = 0.05;
    constexpr f64 MOST_FLICKER = 0.02;
    constexpr u64 HALF_BEAT = 48;
    constexpr u64 HALF_FILLED = 90;
    using enum core::GlowRole;
    core::math::SetFloatingPointEnvironment();
    core::Jobs jobs;
    game::Arena arena(test::TestManifest(), game::Scene::Arena, game::BuiltInData());
    platform::Window window = OpenWindow();
    gfx::Renderer renderer = RendererFor(window, ArenaScene(), jobs);
    GlowRig rig{ .renderer = &renderer, .arena = &arena, .jobs = &jobs };
    const GlowShot beat{ .role = DemoPulse, .tick = 0 };
    const GlowShot halfBeat{ .role = DemoPulse, .tick = HALF_BEAT };
    const GlowShot filling{ .role = DemoProgress, .tick = HALF_FILLED };
    const f64 full = GlowAmount(rig, beat);
    const f64 progress = GlowAmount(rig, filling);
    renderer.SetReduceFlashing(true);
    const f64 held = GlowAmount(rig, beat);
    const f64 heldHalf = GlowAmount(rig, halfBeat);
    const f64 progressReduced = GlowAmount(rig, filling);
    const f64 average = ExpectedGlow(rig.effects, beat, true) / ExpectedGlow(rig.effects, beat, false);
    {
        INFO("an ink pulse held at ", held / full, " of its full strength at its beat's start, GlowStrength ", average);
        INFO("and at ", heldHalf / full, " at its half");
        CHECK(std::abs((heldHalf / held) - 1.0) < MOST_FLICKER);
        CHECK(std::abs((held / full / average) - 1.0) < MOST_ERROR);
    }
    {
        INFO("an ink progress: ", progressReduced / progress, " of its strength without reducing flashing");
        CHECK(std::abs((progressReduced / progress) - 1.0) < MOST_FLICKER);
    }
    // The shrine's pulse as light, over the dark backdrop.
    rig.effects.glows.at(std::to_underlying(DemoPulse)).blend = core::GlowBlend::Light;
    renderer.SetEffects(rig.effects);
    const GlowShot lit{ .role = DemoPulse, .tick = 0, .ground = false };
    const f64 lightReduced = GlowAmount(rig, lit);
    renderer.SetReduceFlashing(false);
    const f64 lightFull = GlowAmount(rig, lit);
    const f64 dimmed = ExpectedGlow(rig.effects, lit, true) / ExpectedGlow(rig.effects, lit, false);
    INFO("a light pulse reducing flashing: ", lightReduced / lightFull, " of its full strength, GlowStrength ", dimmed);
    CHECK(std::abs((lightReduced / lightFull / dimmed) - 1.0) < MOST_ERROR);
}

} // namespace
