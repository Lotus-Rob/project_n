// A seals table read from text and drawn (the phase 6 chunk 4b spec, "Drawing"): seals.yaml is untrusted input, so a table
// the reader accepts, however extreme its values, lays out only finite numbers within their ranges. Only a test that has
// both the reader and the layout can follow a value from the file to the GPU's records.
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/seal.hpp"
#include "core/seals.hpp"
#include "core/tick_clock.hpp"
#include "core/ui_colours.hpp"
#include "data/seals.hpp"
#include "gfx/seal_draw.hpp"
#include "support/manifest.hpp"

#include <doctest/doctest.h>

namespace {

// The shipped manifest's table of `text`, which the reader must take.
core::Seals Parsed(std::string_view text, const core::Manifest& manifest) {
    const std::expected<core::Seals, core::Error> looks = data::ParseSeals(text, manifest);
    const std::string why = looks.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(looks.has_value(), why);
    return looks.value_or(core::Seals{});
}

// `seal` drawn by `table` at `seconds`.
gfx::SealDrawing DrawnAt(const core::Seal& seal, const core::Seals& table, const core::Manifest& manifest, f64 seconds) {
    const gfx::SealPack pack = gfx::PackSeals(table, manifest);
    gfx::SealDrawing drawn;
    const auto tick = static_cast<u64>(seconds * static_cast<f64>(core::TICKS_PER_SECOND));
    gfx::DrawSeals(std::span{ &seal, 1 }, table, pack, core::UiColours{}, seconds, tick, true, drawn);
    return drawn;
}

// Whether every particle of every light in `drawn` lands at finite numbers.
bool ParticlesFinite(const gfx::SealDrawing& drawn) {
    bool finite = true;
    for (const std::vector<gfx::SealGpuParticles>* lights : { &drawn.groundParticles, &drawn.aboveParticles }) {
        for (const gfx::SealGpuParticles& light : *lights) {
            for (u32 index = 0; index < light.pillars + light.sparks + light.shards; ++index) {
                const gfx::SealParticle particle = gfx::SealParticleAt(light, index);
                finite = finite && std::isfinite(particle.centre.x) && std::isfinite(particle.centre.y) &&
                         std::isfinite(particle.along.x) && std::isfinite(particle.along.y) &&
                         std::isfinite(particle.across.x) && std::isfinite(particle.across.y);
            }
        }
    }
    return finite;
}

TEST_CASE("a table at the reader's extremes draws finite numbers within their ranges") {
    // On a seal 1e-15 across, a satellite as small as the reader takes, its shards as fast as it takes, come round every
    // 1e-23 s: by a late render time they have come round far more often than a u64 counts.
    constexpr std::string_view EXTREMES =
        "seals:\n"
        "  big:\n"
        "    plane: overhead\n"
        "    lift: 100000\n"
        "    wash: { darkness: 1, reach: 4, softness: 0, shadow: 1 }\n"
        "    colours: [element]\n"
        "    life: { appear: 1e-300, fade: 1e-300 }\n"
        "    layers:\n"
        "      - inner: 0\n"
        "        outer: 1\n"
        "        height: 100000\n"
        "        spin: 720\n"
        "        frame: { sides: 64 }\n"
        "        parts: [{ kind: star, points: 360, step: 179, radius: 1e-37, width: 1e-37 }]\n"
        "    satellites: [{ seal: speck, layer: 0, size: 0.001, mount: orbit, orbit: -720 }]\n"
        "  speck:\n"
        "    plane: overhead\n"
        "    colours: [element]\n"
        "    layers: [{ inner: 0, outer: 1, spin: -720 }]\n"
        "    light:\n"
        "      pillars: { count: 1, height: 100000, width: 1, fade: 1, cycle: 3600 }\n"
        "      sparks: { count: 1, rise: 100000, life: 0.016666666666666666, size: 1 }\n"
        "      shards: { count: 254, drift: 100000, spin: 720, size: 1 }\n";
    constexpr f64 LATE = 1'000'000.0; // seconds: about eleven days up
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const std::expected<core::Seals, core::Error> looks = data::ParseSeals(EXTREMES, *manifest);
    const std::string why = looks.error_or(core::Error{}).message;
    REQUIRE_MESSAGE(looks.has_value(), why);
    const core::Seals table = looks.value_or(core::Seals{});
    const gfx::SealPack pack = gfx::PackSeals(table, *manifest);
    constexpr f32 TINY = 1e-15F;
    const std::array<core::Seal, 1> seals{ { { .key = core::SealKeyOf("big"), .size = TINY, .endTick = core::NEVER_ENDS } } };
    gfx::SealDrawing drawn;
    const auto tick = static_cast<u64>(LATE * static_cast<f64>(core::TICKS_PER_SECOND));
    gfx::DrawSeals(seals, table, pack, core::UiColours{}, LATE, tick, true, drawn);
    REQUIRE(drawn.aboveParticles.size() == 1);
    const gfx::SealGpuParticles& light = drawn.aboveParticles[0];
    // Each stream's age lies within its period, the shards' however often they came round.
    CHECK((light.pillarAge >= 0.0F && light.pillarAge < light.pillarCycle));
    CHECK((light.sparkAge >= 0.0F && light.sparkAge < light.sparkLife));
    CHECK((light.shardAge >= 0.0F && light.shardAge < light.shardCycle));
    // Every layer and corner is a finite number, and every layer as strong as the cap at most.
    bool finite = true;
    f32 strongest = 0.0F;
    for (const gfx::SealGpuLayer& layer : drawn.layers) {
        finite = finite && std::isfinite(layer.angle) && std::isfinite(layer.opacity) && std::isfinite(layer.centre.x) &&
                 std::isfinite(layer.centre.y);
        strongest = std::max(strongest, layer.opacity);
    }
    for (const std::vector<gfx::SealVertex>* quads :
         { &drawn.groundWash, &drawn.groundLines, &drawn.aboveWash, &drawn.aboveLines }) {
        for (const gfx::SealVertex& vertex : *quads) {
            finite = finite && std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y);
        }
    }
    CHECK(finite);
    CHECK(strongest <= table.aboveOpacity);
}

TEST_CASE("a shard drifting or turning too slowly for an f32 cycle comes round within an hour") {
    // A drift the reader takes (a denormal above 0) on a seal a world unit across would come round every 1e40 s, and the
    // slowest turn it takes every 360,000 s: each is held to an hour, finite as an f32, its age within it.
    constexpr f64 HOUR = 3'600.0;
    constexpr f64 LATE = 100'000.0;
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::Seals table = Parsed(
        "seals:\n"
        "  drifting:\n"
        "    colours: [element]\n"
        "    layers: [{ inner: 0, outer: 1 }]\n"
        "    light: { shards: { count: 4, drift: 1e-40, spin: 0, size: 0.05 } }\n"
        "  turning:\n"
        "    colours: [element]\n"
        "    layers: [{ inner: 0, outer: 1 }]\n"
        "    light: { shards: { count: 4, drift: 0, spin: 0.001, size: 0.05 } }\n",
        *manifest
    );
    for (const std::string_view name : { "drifting", "turning" }) {
        CAPTURE(name);
        const gfx::SealDrawing drawn =
            DrawnAt({ .key = core::SealKeyOf(name), .size = 1.0F, .endTick = core::NEVER_ENDS }, table, *manifest, LATE);
        REQUIRE(drawn.groundParticles.size() == 1);
        const gfx::SealGpuParticles& light = drawn.groundParticles[0];
        CHECK(light.shardCycle == static_cast<f32>(HOUR));
        CHECK((light.shardAge >= 0.0F && light.shardAge < light.shardCycle));
        CHECK(ParticlesFinite(drawn));
    }
}

TEST_CASE("a satellite too small to lay out draws nothing and is counted") {
    // On a seal 2e-36 across, a moon the least size the reader takes is 2e-39 across, below a normal f32, and its speck
    // smaller still: neither is laid out (its shards would divide by its size), both are counted, and the seal draws.
    constexpr f32 SMALL = 2e-36F;
    const std::shared_ptr<const core::Manifest> manifest = test::TestManifest();
    const core::Seals table = Parsed(
        "seals:\n"
        "  planet:\n"
        "    colours: [element]\n"
        "    layers: [{ inner: 0, outer: 1 }]\n"
        "    satellites: [{ seal: moon, size: 0.001 }]\n"
        "  moon:\n"
        "    colours: [element]\n"
        "    layers: [{ inner: 0, outer: 1 }]\n"
        "    satellites: [{ seal: speck, size: 0.001 }]\n"
        "    light: { shards: { count: 4, drift: 1, spin: 30, size: 1 } }\n"
        "  speck:\n"
        "    colours: [element]\n"
        "    layers: [{ inner: 0, outer: 1 }]\n"
        "    light: { shards: { count: 4, drift: 1, spin: 30, size: 1 } }\n",
        *manifest
    );
    const gfx::SealDrawing drawn =
        DrawnAt({ .key = core::SealKeyOf("planet"), .size = SMALL, .endTick = core::NEVER_ENDS }, table, *manifest, 1.0);
    CHECK(drawn.layers.size() == 1);
    CHECK(drawn.groundParticles.empty());
    CHECK(ParticlesFinite(drawn));
    CHECK(drawn.tinySeals == 2);
    CHECK(drawn.droppedSeals == 0);
}

} // namespace
