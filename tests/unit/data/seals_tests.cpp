#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/glow.hpp"
#include "core/manifest.hpp"
#include "core/seal.hpp"
#include "core/seals.hpp"
#include "data/seals.hpp"
#include "platform/paths.hpp"
#include "support/manifest.hpp"

#include <doctest/doctest.h>

namespace {

// One seal with one ring, the least a table holds: the table a reload starts from.
constexpr std::string_view ONE_SEAL = "seals:\n"
                                      "  a:\n"
                                      "    colours: [element]\n"
                                      "    layers:\n"
                                      "      - { inner: 0.5, outer: 1, parts: [{ kind: circle, radius: 0.9, width: 0.01 }] }\n";

core::Error ErrorFor(std::string_view text) {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const std::expected<core::Seals, core::Error> seals = data::ParseSeals(text, *manifest);
    REQUIRE_FALSE(seals.has_value());
    return seals.error();
}

TEST_CASE("the seals reader refuses a value its f32 can't hold above 0, with its line") {
    struct Broken {
        std::string_view why;
        std::string_view text;
        std::string_view says;
    };
    constexpr std::array<Broken, 4> CASES{ {
        { .why = "a width above 0 that narrows to 0",
          .text = "seals:\n  a:\n    colours: [element]\n    layers:\n"
                  "      - { inner: 0, outer: 1, parts: [{ kind: circle, radius: 1, width: 1e-50 }] }\n",
          .says = "seals line 5: a layer 0 part 0's width is too small for an f32" },
        { .why = "a radius that narrows to a denormal",
          .text = "seals:\n  a:\n    colours: [element]\n    layers:\n"
                  "      - { inner: 0, outer: 1, parts: [{ kind: circle, radius: 1e-40, width: 0.1 }] }\n",
          .says = "seals line 5: a layer 0 part 0's radius is too small for an f32" },
        { .why = "a satellite's size that narrows to 0",
          .text = "seals:\n"
                  "  a: { colours: [element], layers: [{ inner: 0, outer: 1 }], satellites: [{ seal: b, size: 1e-50 }] }\n"
                  "  b: { colours: [element], layers: [{ inner: 0, outer: 1 }] }\n",
          .says = "seals line 2: a's satellite 0's size is too small for an f32" },
        { .why = "a view ratio that narrows to 0",
          .text = "view_ratio: 1e-50\n",
          .says = "seals line 1: the seals table's view_ratio is too small for an f32" },
    } };
    for (const Broken& broken : CASES) {
        CAPTURE(broken.why);
        const core::Error error = ErrorFor(broken.text);
        CHECK(error.message == broken.says);
    }
}

TEST_CASE("the seals reader refuses a table past its caps, with its line") {
    // Aliases make each entry cheap to write, so only a cap on the count keeps the table and its pack small.
    const auto repeated = [](std::string_view first, std::string_view rest, u32 count) {
        std::string list{ first };
        for (u32 item = 1; item < count; ++item) {
            list += rest;
        }
        return list;
    };
    struct Broken {
        std::string why;
        std::string text;
        std::string_view says;
    };
    const std::array<Broken, 4> cases{ {
        { .why = "one seal past the table's cap",
          .text = "seals:\n  s0: &s { colours: [element], layers: [{ inner: 0, outer: 1 }] }\n" +
                  [] {
                      std::string more;
                      for (u32 seal = 1; seal <= core::MAX_SEAL_LOOKS; ++seal) {
                          more += std::format("  s{}: *s\n", seal);
                      }
                      return more;
                  }(),
          .says = "seals line 2: the seals table has 1025 seals, past 1024" },
        { .why = "one satellite past a look's cap",
          .text = "seals:\n  a:\n    colours: [element]\n    layers: [{ inner: 0, outer: 1 }]\n    satellites: [" +
                  repeated("&m { seal: b, size: 0.1 }", ", *m", core::MAX_SEAL_SATELLITES + 1) +
                  "]\n  b: { colours: [element], layers: [{ inner: 0, outer: 1 }] }\n",
          .says = "seals line 5: a's satellites isn't a list of at most 63 satellites" },
        { .why = "one sprite past a glyph set's cap",
          .text = "glyph_sets:\n  runes: [" + repeated("&r rune_00", ", *r", core::MAX_GLYPH_SET_SPRITES + 1) + "]\n",
          .says = "seals line 2: glyph set runes isn't a list of 1 to 256 sprites" },
        { .why = "one glyph set past the table's cap",
          .text = "glyph_sets:\n" +
                  [] {
                      std::string sets;
                      for (u32 set = 0; set <= core::MAX_SEAL_GLYPH_SETS; ++set) {
                          sets += std::format("  set{}: [rune_00]\n", set);
                      }
                      return sets;
                  }(),
          .says = "seals line 2: glyph_sets has 65 sets, past 64" },
    } };
    for (const Broken& broken : cases) {
        CAPTURE(broken.why);
        const core::Error error = ErrorFor(broken.text);
        CHECK(error.message == broken.says);
    }
}

TEST_CASE("the shipped seals table loads clean") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const std::expected<std::filesystem::path, core::Error> assets = platform::AssetsDirectory();
    REQUIRE(assets.has_value());
    const std::expected<core::Seals, core::Error> loaded = data::LoadSeals(*assets / "data", *manifest);
    REQUIRE(loaded.has_value());
    const core::SealLook* shipped = loaded->Find(core::SealKeyOf("sword_formation"));
    CHECK(shipped != nullptr);
    // LoadSeals logs its warnings; the same text parsed here hands them back, and there are none.
    std::ifstream input{ *assets / "data" / "seals.yaml", std::ios::binary };
    const std::string text{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    std::vector<std::string> warnings;
    const std::expected<core::Seals, core::Error> parsed = data::ParseSeals(text, *manifest, &warnings);
    REQUIRE(parsed.has_value());
    CHECK(warnings.empty());
    const core::SealLook* swords = parsed->Find(core::SealKeyOf("sword_formation"));
    REQUIRE(swords != nullptr);
    CHECK(swords->plane == core::SealPlane::Ground);
    CHECK(swords->colourCount == 1);
    CHECK(swords->colours.at(0).element);
    CHECK(swords->emblem.has_value());
}

TEST_CASE("every part, the emblem, a satellite and the light are read into the look") {
    constexpr f32 VIEW_RATIO = 0.5F;
    constexpr f32 INNER = 0.5F;
    constexpr f32 HEIGHT = 12.0F;
    constexpr f32 SPIN = -9.0F;
    constexpr f32 DASH = 4.0F;
    constexpr f32 SPILL = 0.25F;
    constexpr f64 SPARK_LIFE = 1.5;
    constexpr f64 APPEAR = 0.4;
    constexpr f32 TICK_LENGTH = 0.03F;
    constexpr f32 PILLAR_FADE = 0.5F;
    constexpr f64 PILLAR_CYCLE = 2.5;
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const std::expected<core::Seals, core::Error> seals = data::ParseSeals(
        "view_ratio: 0.5\n"
        "glyph_sets:\n"
        "  first: [rune_00]\n"
        "  runes: [rune_01, rune_02]\n"
        "seals:\n"
        "  b: { colours: [element], layers: [{ inner: 0, outer: 1 }] }\n"
        "  a:\n"
        "    plane: overhead\n"
        "    colours: [element, effects.seal_gold]\n"
        "    life: { appear: 0.4, appear_curve: smooth, order: inner_first }\n"
        "    layers:\n"
        "      - inner: 0.5\n"
        "        outer: 1\n"
        "        height: 12\n"
        "        spin: -9\n"
        "        frame: { sides: 4, rotation: 45 }\n"
        "        parts:\n"
        "          - { kind: circle, radius: 0.9, width: 0.01, dash: 4, gap: 2 }\n"
        "          - { kind: script, glyphs: runes, count: 12, radius: 0.7, size: 0.1, facing: inward, colour: 1 }\n"
        "          - { kind: star, points: 6, step: 2, radius: 0.6, width: 0.01 }\n"
        "          - { kind: flourish, sprites: [flourish_flame_0, flourish_flame_1], count: 3, radius: 0.8, size: 0.2,\n"
        "              spill: 0.25 }\n"
        "          - { kind: ticks, count: 72, radius: 0.85, length: 0.03, width: 0.003 }\n"
        "          - { kind: polygon, sides: 5, radius: 0.5, width: 0.01 }\n"
        "    emblem: { sprite: emblem_swords, size: 0.2, colour: 1 }\n"
        "    satellites: [{ seal: b, layer: 0, angle: 90, size: 0.2, mount: orbit, orbit: 30 }]\n"
        "    light: { pillars: { count: 2, height: 80, width: 0.05, fade: 0.5, cycle: 2.5 },\n"
        "             sparks: { count: 24, rise: 40, life: 1.5 } }\n",
        *manifest
    );
    REQUIRE(seals.has_value());
    CHECK(seals->viewRatio == VIEW_RATIO);
    CHECK(seals->glyphSets.size() == 2);
    CHECK(seals->looks.size() == 2);
    const core::SealLook* look = seals->Find(core::SealKeyOf("a"));
    REQUIRE(look != nullptr);
    CHECK(look->plane == core::SealPlane::Overhead);
    CHECK(look->colourCount == 2);
    CHECK(look->colours.at(1).colour == core::UiColour::SealGold);
    CHECK(look->appear == APPEAR);
    CHECK(look->appearCurve == core::GlowCurve::Smooth);
    CHECK(look->innerFirst);
    REQUIRE(look->layers.size() == 1);
    const core::SealLayer& layer = look->layers.front();
    CHECK(layer.inner == INNER);
    CHECK(layer.height == HEIGHT);
    CHECK(layer.spin == SPIN); // negative: the other way
    CHECK(layer.frameSides == 4);
    REQUIRE(layer.parts.size() == 6);
    CHECK(layer.parts.at(0).dash == DASH);
    const core::SealPart& script = layer.parts.at(1);
    CHECK(script.kind == core::SealPartKind::Script);
    CHECK(script.glyphSet == 1); // the second set listed
    CHECK(script.inward);
    CHECK(script.slot == 1);
    CHECK(layer.parts.at(2).step == 2);
    const core::SealPart& flourish = layer.parts.at(3);
    CHECK(flourish.spill == SPILL);
    // Two sprites given fill the three in turn.
    CHECK(flourish.sprites.at(1) == core::atlas::SpriteId::FlourishFlame1);
    CHECK(flourish.sprites.at(2) == core::atlas::SpriteId::FlourishFlame0);
    CHECK(layer.parts.at(4).size == TICK_LENGTH); // a tick's length is its size
    CHECK(layer.parts.at(5).count == 5);          // a polygon's sides are its count
    REQUIRE(look->emblem.has_value());
    CHECK(look->emblem.value_or(core::SealEmblem{}).sprite == core::atlas::SpriteId::EmblemSwords);
    REQUIRE(look->satellites.size() == 1);
    CHECK(look->satellites.front().seal == core::SealKeyOf("b"));
    CHECK(look->satellites.front().mount == core::SealMount::Orbit);
    CHECK(look->light.sparks == 24);
    CHECK(look->light.sparkLife == SPARK_LIFE);
    CHECK(look->light.pillarFade == PILLAR_FADE);
    CHECK(look->light.pillarCycle == PILLAR_CYCLE);
    const core::SealLook* missing = seals->Find(core::SealKeyOf("c"));
    CHECK(missing == nullptr);
}

TEST_CASE("the seals reader refuses out-of-range values with their line") {
    struct Broken {
        std::string_view why;
        std::string_view text;
        std::string_view says;
    };
    constexpr std::array<Broken, 21> CASES{ {
        { .why = "no ring",
          .text = "seals:\n  a:\n    colours: [element]\n    layers: []\n",
          .says = "seals line 4: a's layers isn't a list of 1 to 9 layers" },
        { .why = "one ring past the limit",
          .text = "seals:\n  a:\n    colours: [element]\n"
                  "    layers: [&l { inner: 0, outer: 1 }, *l, *l, *l, *l, *l, *l, *l, *l, *l]\n",
          .says = "seals line 4: a's layers isn't a list of 1 to 9 layers" },
        { .why = "one part past the limit",
          .text = "seals:\n  a:\n    colours: [element]\n    layers:\n"
                  "      - inner: 0\n        outer: 1\n"
                  "        parts: [&p { kind: circle, radius: 1, width: 0.1 }, *p, *p, *p, *p, *p, *p, *p, *p, *p, *p, *p,\n"
                  "                *p, *p, *p, *p, *p]\n",
          .says = "seals line 7: a layer 0's parts isn't a list of at most 16 parts" },
        { .why = "a width of 0",
          .text = "seals:\n  a:\n    colours: [element]\n    layers:\n"
                  "      - { inner: 0, outer: 1, parts: [{ kind: circle, radius: 1, width: 0 }] }\n",
          .says = "seals line 5: a layer 0 part 0's width isn't" },
        { .why = "an inner radius at the outer one",
          .text = "seals:\n  a:\n    colours: [element]\n    layers:\n      - { inner: 1, outer: 1 }\n",
          .says = "seals line 5: a layer 0's inner radius isn't below its outer one" },
        { .why = "a palette name that isn't one",
          .text = "seals:\n  a:\n    colours: [element, effects.shiny]\n    layers: [{ inner: 0, outer: 1 }]\n",
          .says = "seals line 3: a has the colour \"effects.shiny\"" },
        { .why = "a curve that isn't one",
          .text = "seals:\n  a:\n    colours: [element]\n    life: { fade_curve: bumpy }\n"
                  "    layers: [{ inner: 0, outer: 1 }]\n",
          .says = "seals line 4: a's life's fade_curve \"bumpy\" isn't known" },
        { .why = "a sprite the atlas lacks",
          .text = "seals:\n  a:\n    colours: [element]\n    layers: [{ inner: 0, outer: 1 }]\n"
                  "    emblem: { sprite: emblem_moon, size: 0.2 }\n",
          .says = "seals line 5: a's emblem names the sprite \"emblem_moon\", which isn't in the atlas" },
        { .why = "a glyph set that isn't there",
          .text = "seals:\n  a:\n    colours: [element]\n    layers:\n"
                  "      - { inner: 0, outer: 1, parts: [{ kind: script, glyphs: runes, count: 4, radius: 1, size: 0.1 }] }\n",
          .says = "seals line 5: a layer 0 part 0 names the glyph set \"runes\", which isn't there" },
        { .why = "a satellite of itself",
          .text = "seals:\n  a:\n    colours: [element]\n    layers: [{ inner: 0, outer: 1 }]\n"
                  "    satellites: [{ seal: a, size: 0.2 }]\n",
          .says = "seals line 5: a's satellite names its own seal" },
        { .why = "two seals each the other's satellite",
          .text = "seals:\n"
                  "  a: { colours: [element], layers: [{ inner: 0, outer: 1 }], satellites: [{ seal: b, size: 0.2 }] }\n"
                  "  b: { colours: [element], layers: [{ inner: 0, outer: 1 }], satellites: [{ seal: a, size: 0.2 }] }\n",
          .says = "seals line 3: b's satellite a forms a loop" },
        { .why = "satellites one deeper than the limit",
          .text = "seals:\n"
                  "  a: { colours: [element], layers: [{ inner: 0, outer: 1 }], satellites: [{ seal: b, size: 0.2 }] }\n"
                  "  b: { colours: [element], layers: [{ inner: 0, outer: 1 }], satellites: [{ seal: c, size: 0.2 }] }\n"
                  "  c: { colours: [element], layers: [{ inner: 0, outer: 1 }], satellites: [{ seal: d, size: 0.2 }] }\n"
                  "  d: { colours: [element], layers: [{ inner: 0, outer: 1 }] }\n",
          .says = "seals line 2: a's satellites nest 3 deep, past 2" },
        { .why = "a flat view",
          .text = "view_ratio: 0\n",
          .says = "seals line 1: the seals table's view_ratio isn't a number above 0 and up to 1" },
        { .why = "a negative appear time",
          .text = "seals:\n  a:\n    colours: [element]\n    life: { appear: -1 }\n"
                  "    layers: [{ inner: 0, outer: 1 }]\n",
          .says = "seals line 4: a's life's appear isn't" },
        { .why = "one spark past the limit",
          .text = "seals:\n  a:\n    colours: [element]\n    layers: [{ inner: 0, outer: 1 }]\n"
                  "    light: { sparks: { count: 257, rise: 40, life: 1 } }\n",
          .says = "seals line 5: a's spark stream's count isn't a whole number from 0 to 256" },
        { .why = "a pillar fading past its length",
          .text = "seals:\n  a:\n    colours: [element]\n    layers: [{ inner: 0, outer: 1 }]\n"
                  "    light: { pillars: { count: 2, height: 80, width: 0.05, fade: 1.5, cycle: 2 } }\n",
          .says = "seals line 5: a's pillar stream's fade isn't a number from 0 to 1" },
        { .why = "a pillar whose light never falls",
          .text = "seals:\n  a:\n    colours: [element]\n    layers: [{ inner: 0, outer: 1 }]\n"
                  "    light: { pillars: { count: 2, height: 80, width: 0.05, fade: 0.5, cycle: 0 } }\n",
          .says = "seals line 5: a's pillar stream's cycle isn't a number above 0" },
        { .why = "a colour slot the seal lacks",
          .text = "seals:\n  a:\n    colours: [element]\n    layers:\n"
                  "      - { inner: 0, outer: 1, parts: [{ kind: circle, radius: 1, width: 0.1, colour: 1 }] }\n",
          .says = "seals line 5: a layer 0 part 0 takes colour 1, but a has only colour 0" },
        { .why = "a satellite naming a seal that isn't there",
          .text = "seals:\n  a:\n    colours: [element]\n    layers: [{ inner: 0, outer: 1 }]\n"
                  "    satellites: [{ seal: moon, size: 0.2 }]\n",
          .says = "seals line 5: a's satellite names the seal \"moon\", which isn't there" },
        { .why = "a star whose step is half its points",
          .text = "seals:\n  a:\n    colours: [element]\n    layers:\n"
                  "      - { inner: 0, outer: 1, parts: [{ kind: star, points: 6, step: 3, radius: 1, width: 0.1 }] }\n",
          .says = "seals line 5: a layer 0 part 0's step isn't below half its points" },
        { .why = "a layer that isn't a map: refused as ours, not as yaml-cpp's bad subscript",
          .text = "seals:\n  a:\n    colours: [element]\n    layers: [5]\n",
          .says = "seals line 4: a layer 0 isn't a map" },
    } };
    for (const Broken& broken : CASES) {
        CAPTURE(broken.why);
        const core::Error error = ErrorFor(broken.text);
        CHECK(error.message.starts_with(broken.says));
    }
}

TEST_CASE("the seals reader warns on unknown keys and reads the rest") {
    constexpr f32 OUTER = 0.75F;
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    std::vector<std::string> warnings;
    const std::expected<core::Seals, core::Error> seals = data::ParseSeals(
        "wobble: 1\n"
        "seals:\n"
        "  a:\n"
        "    colours: [element]\n"
        "    glitter: 2\n"
        "    layers:\n"
        "      - { inner: 0.5, outer: 0.75, tilt: 3, parts: [{ kind: dots, count: 4, radius: 0.6, size: 0.05, dash: 1 }] }\n",
        *manifest,
        &warnings
    );
    REQUIRE(seals.has_value());
    REQUIRE(warnings.size() == 4);
    CHECK(warnings[0] == "seals line 1: there is no section wobble; it is ignored");
    CHECK(warnings[1] == "seals line 5: a has no key glitter; it is ignored");
    CHECK(warnings[2] == "seals line 7: a layer 0 has no key tilt; it is ignored");
    // A key of another kind of part is unknown to this one.
    CHECK(warnings[3] == "seals line 7: a layer 0 part 0 has no key dash; it is ignored");
    const core::SealLook* look = seals->Find(core::SealKeyOf("a"));
    REQUIRE(look != nullptr);
    CHECK(look->layers.front().outer == OUTER);
    CHECK(look->layers.front().parts.front().count == 4);
    CHECK(look->layers.front().parts.front().dash == 0.0F);
}

TEST_CASE("a broken seals table keeps the old one") {
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    std::expected<core::Seals, core::Error> first = data::ParseSeals(ONE_SEAL, *manifest);
    REQUIRE(first.has_value());
    const core::Seals current = std::move(*first);
    // An edit whose first seal reads, but whose second doesn't: the reader hands back no part of it, so the caller keeps
    // the table it had (App's reload keeps `current` on an error).
    const std::expected<core::Seals, core::Error> edited = data::ParseSeals(
        "seals:\n"
        "  b: { colours: [element], layers: [{ inner: 0, outer: 1 }] }\n"
        "  c: { colours: [element], layers: [] }\n",
        *manifest
    );
    REQUIRE_FALSE(edited.has_value());
    CHECK(edited.error().message.starts_with("seals line 3: c's layers"));
    CHECK(current.looks.size() == 1);
    const core::SealLook* kept = current.Find(core::SealKeyOf("a"));
    CHECK(kept != nullptr);
    const core::SealLook* gone = current.Find(core::SealKeyOf("b"));
    CHECK(gone == nullptr);
}

} // namespace
