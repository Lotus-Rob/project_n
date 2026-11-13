#include "gfx/seal_draw.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/glow.hpp"
#include "core/profile.hpp"
#include "core/random.hpp"
#include "core/tick_clock.hpp"
#include "gfx/mesh_vertex.hpp"
#include "gfx/sprite_block.hpp"

namespace gfx {

namespace {

constexpr f64 SEAL_TURN_DEGREES = 360.0;
constexpr f64 SEAL_RADIANS_A_DEGREE = std::numbers::pi / (SEAL_TURN_DEGREES / 2.0);
constexpr f64 SEAL_TICKS_A_SECOND = static_cast<f64>(core::TICKS_PER_SECOND);
constexpr f64 SEAL_FOREVER = std::numeric_limits<f64>::infinity();
constexpr f32 SEAL_TAU = 2.0F * std::numbers::pi_v<f32>;
constexpr u32 SEAL_HIGH_HALF = 32;

f32 SealRadians(f32 degrees) {
    return static_cast<f32>(static_cast<f64>(degrees) * SEAL_RADIANS_A_DEGREE);
}

// The turn is wrapped in f64 before it narrows, so an f32 angle keeps its precision however long the seal has been up.
f32 SealSpin(f32 degreesASecond, f64 seconds, f32 spinScale) {
    const f64 turned = static_cast<f64>(degreesASecond) * static_cast<f64>(spinScale) * seconds;
    return static_cast<f32>(std::fmod(turned, SEAL_TURN_DEGREES) * SEAL_RADIANS_A_DEGREE);
}

f32 SealWrappedAge(f64 seconds, f64 period) {
    if (!(period > 0.0)) {
        return 0.0F;
    }
    const auto age = static_cast<f32>(std::fmod(seconds, period));
    // An age just short of its period can round up to it as an f32: that is the period's start again.
    return age < static_cast<f32>(period) ? age : 0.0F;
}

// How many layers of `look` appear before `layer`: by outer radius, ties in the look's order.
std::size_t SealAppearTurn(const core::SealLook& look, std::size_t layer) {
    const f32 sign = look.innerFirst ? 1.0F : -1.0F;
    const std::pair<f32, std::size_t> mine{ sign * look.layers[layer].outer, layer };
    std::size_t turn = 0;
    for (std::size_t other = 0; other < look.layers.size(); ++other) {
        if (std::pair{ sign * look.layers[other].outer, other } < mine) {
            ++turn;
        }
    }
    return turn;
}

// A seal's times at the render clock, in seconds.
struct SealTimes {
    f64 since = 0.0;
    f64 toEnd = 0.0;
};

// Where a seal's look puts the points of its layers.
struct SealPlace {
    const core::Seal* seal = nullptr;
    core::SealPlane plane = core::SealPlane::Ground;
    f32 lift = 0.0F;
    f32 viewRatio = 0.0F;

    // `local` (in seal sizes) on the plane, `height` (in world units) off it.
    [[nodiscard]] core::Vec2 At(core::Vec2 local, f32 height) const {
        return SealToWorld(
            *seal, plane, lift, viewRatio, { .x = local.x * seal->size, .depth = local.y * seal->size, .up = height }
        );
    }
};

// What every seal of a frame shares.
struct SealFrame {
    const core::Seals* looks = nullptr;
    const SealPack* pack = nullptr;
    const core::UiColours* colours = nullptr;
    SealDrawing* out = nullptr;
    std::size_t drawn = 0;
};

// The ring's corners, the first repeated last to close it.
const std::array<core::Vec2, SEAL_BAND_SEGMENTS + 1>& SealRingDirections() {
    static const std::array<core::Vec2, SEAL_BAND_SEGMENTS + 1> directions = [] {
        std::array<core::Vec2, SEAL_BAND_SEGMENTS + 1> corners{};
        for (u32 corner = 0; corner < SEAL_BAND_SEGMENTS; ++corner) {
            const f32 angle = SEAL_TAU * static_cast<f32>(corner) / static_cast<f32>(SEAL_BAND_SEGMENTS);
            corners.at(corner) = { .x = std::cos(angle), .y = std::sin(angle) };
        }
        corners.back() = corners.front();
        return corners;
    }();
    return directions;
}

// Adds a band of `sides` quads from `inner` to `outer` (seal sizes), `direction(k)` giving corner k's way out from the
// centre, corner `sides` closing it.
void SealAddBand(
    const SealPlace& place,
    u32 sides,
    const auto& direction,
    f32 inner,
    f32 outer,
    f32 height,
    u32 layer,
    std::vector<SealVertex>& lines
) {
    const auto corner = [&](core::Vec2 way, f32 radius) {
        const core::Vec2 local = way * radius;
        return SealVertex{ .position = place.At(local, height), .local = local, .layer = layer };
    };
    for (u32 side = 0; side < sides; ++side) {
        const core::Vec2 start = direction(side);
        const core::Vec2 end = direction(side + 1);
        lines.push_back(corner(start, inner));
        lines.push_back(corner(end, inner));
        lines.push_back(corner(start, outer));
        lines.push_back(corner(end, outer));
    }
}

// A round band: its quads' outer edges lie outside the circle `outer`, so their chords never cut into what it holds.
void SealAddRoundBand(const SealPlace& place, f32 inner, f32 outer, f32 height, u32 layer, std::vector<SealVertex>& lines) {
    static const f32 circumscribe = 1.0F / std::cos(std::numbers::pi_v<f32> / static_cast<f32>(SEAL_BAND_SEGMENTS));
    const auto& ring = SealRingDirections();
    SealAddBand(
        place,
        SEAL_BAND_SEGMENTS,
        [&](u32 corner) { return ring.at(corner); },
        inner,
        outer * circumscribe,
        height,
        layer,
        lines
    );
}

SealGpuSprite SealSpriteOf(const core::Manifest& manifest, core::atlas::SpriteId sprite) {
    const core::SpriteInfo& info = manifest.Sprite(sprite);
    const SpriteBlock block = FieldBlockOf(info);
    const auto page = static_cast<f32>(manifest.pageSize);
    return { .fieldMin = { .x = static_cast<f32>(block.x) / page, .y = static_cast<f32>(block.y) / page },
             .fieldMax = { .x = static_cast<f32>(block.x + block.width) / page,
                           .y = static_cast<f32>(block.y + block.height) / page },
             .width = static_cast<f32>(info.width),
             .height = static_cast<f32>(info.height),
             .page = info.page };
}

// `part` packed, its sprites added to `pack` but for a script's, which are its glyph set's, from `glyphSets`' first.
SealGpuPart SealPartOf(
    SealPack& pack,
    const core::Seals& seals,
    const core::Manifest& manifest,
    std::span<const u32> glyphSets,
    const core::SealPart& part
) {
    SealGpuPart gpu{ .kind = std::to_underlying(part.kind),
                     .slot = part.slot,
                     .radius = part.radius,
                     .width = part.width,
                     .size = part.size,
                     .count = part.count,
                     .step = part.step,
                     .dash = SealRadians(part.dash),
                     .gap = SealRadians(part.gap),
                     .rotation = SealRadians(part.rotation),
                     .spill = part.spill,
                     .inward = part.inward ? 1U : 0U };
    if (part.kind == core::SealPartKind::Script && part.glyphSet < glyphSets.size()) {
        gpu.firstSprite = glyphSets[part.glyphSet];
        gpu.spriteCount = static_cast<u32>(seals.glyphSets[part.glyphSet].size());
    }
    else if (part.kind == core::SealPartKind::Flourish) {
        gpu.firstSprite = static_cast<u32>(pack.sprites.size());
        gpu.spriteCount = static_cast<u32>(part.sprites.size());
        for (const core::atlas::SpriteId sprite : part.sprites) {
            pack.sprites.push_back(SealSpriteOf(manifest, sprite));
        }
    }
    return gpu;
}

// Where `part`'s band starts, in seal sizes from the centre, before the margin every part's band shares (the halo and a
// pixel): shaders/seal.frag.hlsl's PartBand() works it out alike, and stops at the first part whose band starts past the
// pixel, so the order must be this.
f32 SealBandStart(const core::SealPart& part) {
    using Kind = core::SealPartKind;
    if (part.kind == Kind::Circle) {
        return part.radius - (part.width * SEAL_HALF);
    }
    if (part.kind == Kind::Ticks || part.kind == Kind::Dots) {
        return part.radius - (std::max(part.size, part.width) * SEAL_HALF);
    }
    if (part.kind == Kind::Star || part.kind == Kind::Polygon) {
        // Its lines come nearest the centre at its apothem: the chord joining points `joins` apart.
        const f32 joins = part.kind == Kind::Star ? static_cast<f32>(part.step) : 1.0F;
        const f32 apothem =
            part.count > 0 ? part.radius * std::cos(std::numbers::pi_v<f32> * joins / static_cast<f32>(part.count)) : 0.0F;
        return apothem - (part.width * SEAL_HALF);
    }
    return part.radius - part.size; // a glyph or a flourish
}

// `layer`'s parts packed, sorted by where their bands start; ties keep the look's order.
SealPackedLayer SealPackLayer(
    SealPack& pack,
    const core::Seals& seals,
    const core::Manifest& manifest,
    std::span<const u32> glyphSets,
    const core::SealLayer& layer
) {
    SealPackedLayer packed{ .firstPart = static_cast<u32>(pack.parts.size()) };
    std::array<std::pair<f32, std::size_t>, core::MAX_SEAL_PARTS> order{};
    const std::size_t partCount = std::min<std::size_t>(layer.parts.size(), core::MAX_SEAL_PARTS);
    for (std::size_t part = 0; part < partCount; ++part) {
        order.at(part) = { SealBandStart(layer.parts[part]), part };
    }
    std::ranges::sort(std::span{ order }.first(partCount));
    for (const auto& entry : std::span{ order }.first(partCount)) {
        const core::SealPart& part = layer.parts[entry.second];
        pack.parts.push_back(SealPartOf(pack, seals, manifest, glyphSets, part));
        packed.spill = std::max(packed.spill, part.kind == core::SealPartKind::Flourish ? part.spill : 0.0F);
    }
    packed.partCount = static_cast<u32>(partCount);
    return packed;
}

// A layer of the seal `place` puts, at `height`: its centre and the plane's axes there.
SealGpuLayer SealLayerAt(const SealPlace& place, f32 height, const SealGpuLayer& shared) {
    SealGpuLayer layer = shared;
    layer.centre = place.At({}, height);
    layer.axisX = place.At({ .x = 1.0F }, height) - layer.centre;
    layer.axisDepth = place.At({ .y = 1.0F }, height) - layer.centre;
    return layer;
}

// What a seal's light takes from its seal besides its look.
struct SealLightShared {
    f32 spinScale = 0.0F;
    u32 colour = 0;
    f32 opacity = 0.0F;
    u64 salt = 0;
};

// Adds `look`'s light, its motion scaled by spin_scale through its ages.
void SealAddLight(
    SealDrawing& out,
    const SealPlace& place,
    const core::SealLook& look,
    const SealTimes& times,
    const SealLightShared& shared
) {
    const core::SealLight& light = look.light;
    if (light.pillars + light.sparks + light.shards == 0) {
        return;
    }
    const core::Seal& seal = *place.seal;
    const f64 age = times.since * static_cast<f64>(shared.spinScale);
    // A shard comes round once it has drifted a seal's size, or turned once if it only turns.
    f64 shardCycle = 0.0;
    if (light.shardDrift > 0.0F) {
        shardCycle = static_cast<f64>(seal.size) / static_cast<f64>(light.shardDrift);
    }
    else if (std::abs(light.shardSpin) > 0.0F) {
        shardCycle = SEAL_TURN_DEGREES / static_cast<f64>(std::abs(light.shardSpin));
    }
    const SealGpuLayer plane = SealLayerAt(place, 0.0F, {});
    const u32 first = out.particles.empty() ? 0
                                            : out.particles.back().firstParticle + out.particles.back().pillars +
                                                  out.particles.back().sparks + out.particles.back().shards;
    out.particles.push_back(
        { .centre = plane.centre,
          .axisX = plane.axisX,
          .axisDepth = plane.axisDepth,
          .seed = static_cast<u32>(
              core::detail::Mix(core::detail::Mix(core::detail::Mix(seal.key) ^ seal.startTick) ^ shared.salt)
          ),
          .colour = shared.colour,
          .opacity = shared.opacity,
          .size = seal.size,
          .firstParticle = first,
          .pillars = light.pillars,
          .sparks = light.sparks,
          .shards = light.shards,
          .pillarAge = SealWrappedAge(age, light.pillarCycle),
          .pillarCycle = static_cast<f32>(light.pillarCycle),
          .pillarHeight = light.pillarHeight,
          .pillarWidth = light.pillarWidth * seal.size,
          .pillarFade = light.pillarFade,
          .sparkAge = SealWrappedAge(age, light.sparkLife),
          .sparkLife = static_cast<f32>(light.sparkLife),
          .sparkRise = light.sparkRise,
          .shardAge = SealWrappedAge(age, shardCycle),
          .shardCycle = static_cast<f32>(shardCycle),
          .shardDrift = light.shardDrift,
          .shardSpin = SealRadians(light.shardSpin) }
    );
}

// What a seal's layers share.
struct SealLayers {
    SealPlace place{};
    const SealTimes* times = nullptr;
    f32 cap = 0.0F;
    std::vector<SealVertex>* lines = nullptr;
    SealGpuLayer shared{};
};

// Adds `layer`, packed as `packed`, its life `life`: its record and its band.
void SealAddLayer(
    const SealFrame& frame,
    const SealLayers& layers,
    const core::SealLayer& layer,
    const SealPackedLayer& packed,
    f32 life
) {
    const core::Seals& looks = *frame.looks;
    SealDrawing& out = *frame.out;
    SealGpuLayer gpu = SealLayerAt(layers.place, layer.height, layers.shared);
    gpu.angle = SealLayerAngle(layer, layers.times->since, looks.spinScale);
    gpu.opacity = std::min(layer.opacity * life, layers.cap) * looks.brightness;
    gpu.firstPart = packed.firstPart;
    gpu.partCount = packed.partCount;
    gpu.inner = layer.inner;
    gpu.outer = layer.outer;
    gpu.frameSides = layer.frameSides;
    gpu.frameRotation = SealRadians(layer.frameRotation);
    const f32 spill = packed.spill;
    const auto index = static_cast<u32>(out.layers.size());
    out.layers.push_back(gpu);
    if (layer.frameSides >= 3) {
        const f32 first = gpu.frameRotation + gpu.angle;
        const f32 step = SEAL_TAU / static_cast<f32>(layer.frameSides);
        const auto corner = [first, step](u32 side) {
            const f32 angle = first + (step * static_cast<f32>(side));
            return core::Vec2{ .x = std::cos(angle), .y = std::sin(angle) };
        };
        SealAddBand(
            layers.place, layer.frameSides, corner, layer.inner, layer.outer + spill, layer.height, index, *layers.lines
        );
    }
    else {
        SealAddRoundBand(layers.place, layer.inner, layer.outer + spill, layer.height, index, *layers.lines);
    }
}

// Adds `emblem`, packed as `packed`'s, as a layer of its own at `height`, its life `life`.
void SealAddEmblem(
    const SealFrame& frame,
    const SealLayers& layers,
    const core::SealEmblem& emblem,
    const SealPackedLook& packed,
    f32 height,
    f32 life
) {
    const core::Seals& looks = *frame.looks;
    SealDrawing& out = *frame.out;
    SealGpuLayer gpu = SealLayerAt(layers.place, height, layers.shared);
    gpu.angle = SealSpin(emblem.spin, layers.times->since, looks.spinScale);
    gpu.opacity = std::min(life, layers.cap) * looks.brightness;
    gpu.firstPart = packed.emblemPart;
    gpu.partCount = 1;
    gpu.outer = packed.emblemReach;
    const auto index = static_cast<u32>(out.layers.size());
    out.layers.push_back(gpu);
    SealAddRoundBand(layers.place, 0.0F, packed.emblemReach, height, index, *layers.lines);
}

// `emblem` packed into `pack`: its part, its sprite, and the reach of the disc that holds its distance field.
void SealPackEmblem(SealPack& pack, const core::Manifest& manifest, const core::SealEmblem& emblem, SealPackedLook& packed) {
    const core::SpriteInfo& info = manifest.Sprite(emblem.sprite);
    const f32 texel = emblem.size / static_cast<f32>(info.height);
    constexpr auto REACH = static_cast<f32>(2 * core::FIELD_REACH_TEXELS);
    constexpr f32 CENTRE_TO_CORNER = 0.5F; // of the field block's diagonal
    packed.emblemPart = static_cast<u32>(pack.parts.size());
    packed.emblemReach =
        CENTRE_TO_CORNER * texel * std::hypot(static_cast<f32>(info.width) + REACH, static_cast<f32>(info.height) + REACH);
    pack.parts.push_back(
        { .kind = SEAL_EMBLEM_KIND,
          .slot = emblem.slot,
          .size = emblem.size,
          .count = 1,
          .firstSprite = static_cast<u32>(pack.sprites.size()),
          .spriteCount = 1 }
    );
    pack.sprites.push_back(SealSpriteOf(manifest, emblem.sprite));
}

// `look` packed into `pack`: its layers' parts, then its emblem's.
SealPackedLook SealPackLook(
    SealPack& pack,
    const core::Seals& seals,
    const core::Manifest& manifest,
    std::span<const u32> glyphSets,
    const core::SealLook& look
) {
    SealPackedLook packed{ .firstLayer = static_cast<u32>(pack.layers.size()) };
    for (const core::SealLayer& layer :
         std::span{ look.layers }.first(std::min<std::size_t>(look.layers.size(), core::MAX_SEAL_LAYERS))) {
        pack.layers.push_back(SealPackLayer(pack, seals, manifest, glyphSets, layer));
    }
    if (look.emblem) {
        SealPackEmblem(pack, manifest, *look.emblem, packed);
    }
    return packed;
}

// Adds one seal by its look, packed as `packed`: its layers and their bands, its emblem, its wash and its light.
void SealDrawLook(
    SealFrame& frame,
    const core::Seal& seal,
    const core::SealLook& look,
    const SealPackedLook& packed,
    const SealTimes& times,
    u64 salt
) {
    const core::Seals& looks = *frame.looks;
    SealDrawing& out = *frame.out;
    const std::size_t layerCount = std::min<std::size_t>(look.layers.size(), core::MAX_SEAL_LAYERS);
    if (layerCount == 0) {
        return;
    }
    const bool above = look.plane != core::SealPlane::Ground;
    std::vector<SealVertex>& lines = above ? out.aboveLines : out.groundLines;
    const f32 cap = above ? looks.aboveOpacity : 1.0F;
    const SealPlace place{ .seal = &seal, .plane = look.plane, .lift = look.lift, .viewRatio = looks.viewRatio };
    // Spin_scale slows the life too; at 0 nothing moves, so the seal shows whole from its start to its end.
    const bool still = !(looks.spinScale > 0.0F);
    const f64 lifeSince = still ? SEAL_FOREVER : times.since * static_cast<f64>(looks.spinScale);
    const f64 lifeToEnd = still ? SEAL_FOREVER : times.toEnd * static_cast<f64>(looks.spinScale);
    std::array<f32, core::MAX_SEAL_LAYERS> lives{};
    f32 sealLife = 0.0F;
    std::size_t innermost = 0;
    for (std::size_t index = 0; index < layerCount; ++index) {
        lives.at(index) = SealLifeOpacity(look, index, lifeSince, lifeToEnd);
        sealLife = std::max(sealLife, lives.at(index));
        if (look.layers[index].outer < look.layers[innermost].outer) {
            innermost = index;
        }
    }
    SealGpuLayer shared{ .washColour = frame.colours->Of(core::UiColour::TextInk),
                         .washOpacity = std::min(look.washDarkness * sealLife, cap),
                         .washReach = look.washReach,
                         .washSoftness = look.washSoftness };
    for (std::size_t slot = 0; slot < shared.colours.size(); ++slot) {
        const core::GlowColour& colour = look.colours.at(slot < look.colourCount ? slot : 0);
        shared.colours.at(slot) = frame.colours->Of(colour.element ? core::EffectColour(seal.element) : colour.colour);
    }
    const SealLayers layers{ .place = place, .times = &times, .cap = cap, .lines = &lines, .shared = shared };
    const auto firstLayer = static_cast<u32>(out.layers.size());
    for (std::size_t index = 0; index < layerCount; ++index) {
        SealAddLayer(frame, layers, look.layers[index], frame.pack->layers[packed.firstLayer + index], lives.at(index));
    }
    if (look.emblem) {
        SealAddEmblem(frame, layers, *look.emblem, packed, look.layers[innermost].height, lives.at(innermost));
    }
    if (look.washDarkness > 0.0F) {
        const f32 reach = look.washReach;
        const std::array<core::Vec2, QUAD_VERTICES> corners{ { { .x = -reach, .y = -reach },
                                                               { .x = reach, .y = -reach },
                                                               { .x = -reach, .y = reach },
                                                               { .x = reach, .y = reach } } };
        std::vector<SealVertex>& wash = above ? out.aboveWash : out.groundWash;
        for (const core::Vec2 corner : corners) {
            wash.push_back({ .position = place.At(corner, 0.0F), .local = corner, .layer = firstLayer });
        }
    }
    SealAddLight(
        out,
        place,
        look,
        times,
        { .spinScale = looks.spinScale,
          .colour = shared.colours.at(std::min<std::size_t>(look.light.slot, shared.colours.size() - 1)),
          .opacity = std::min(sealLife, cap) * looks.brightness,
          .salt = salt }
    );
}

// Where the look of `key` is in `seals`' looks, if it is there.
std::optional<std::size_t> SealLookIndex(const core::Seals& seals, core::SealKey key) {
    const auto& looks = seals.looks;
    const auto found = std::ranges::lower_bound(looks, key, {}, &std::pair<core::SealKey, core::SealLook>::first);
    if (found == looks.end() || found->first != key) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - looks.begin());
}

// How many seals drawing `look` `depth` satellites deep adds, its satellites' counts one deeper already in `pack`: itself,
// and each satellite DrawSeals() would draw (not one on a ring its look lacks, or naming a look the table lacks) with its
// own, above the deepest nesting.
u32 SealsDrawnAt(const core::Seals& seals, const SealPack& pack, const core::SealLook& look, u32 depth) {
    u32 count = 1;
    if (depth >= core::MAX_SATELLITE_DEPTH) {
        return count;
    }
    for (const core::SealSatellite& satellite : look.satellites) {
        const std::optional<std::size_t> moon = SealLookIndex(seals, satellite.seal);
        if (satellite.layer < look.layers.size() && moon && *moon < pack.looks.size()) {
            count += pack.looks[*moon].seals.at(depth + 1);
        }
    }
    return count;
}

// Adds `seal`, a satellite `depth` deep (0 for one in play), if the frame has room; its look then, so its satellites are
// drawn next. Null when the table (or its pack) hasn't its look, or when the frame is full: then it is counted with all
// it would have drawn, which is not walked.
const core::SealLook* SealDrawOne(SealFrame& frame, const core::Seal& seal, const SealTimes& times, u64 salt, u32 depth) {
    const std::optional<std::size_t> index = SealLookIndex(*frame.looks, seal.key);
    if (!index || *index >= frame.pack->looks.size()) {
        return nullptr;
    }
    const SealPackedLook& packed = frame.pack->looks[*index];
    if (frame.drawn >= MAX_SEALS_DRAWN) {
        frame.out->droppedSeals += packed.seals.at(depth);
        return nullptr;
    }
    ++frame.drawn;
    const core::SealLook* look = &frame.looks->looks[*index].second;
    SealDrawLook(frame, seal, *look, packed, times, salt);
    return look;
}

// Satellite `index` of `seal` (whose look is `look`) as a seal of its own, on its ring where its mount puts it now.
std::optional<core::Seal> SealSatelliteOf(
    const SealFrame& frame,
    const core::Seal& seal,
    const core::SealLook& look,
    std::size_t index,
    const SealTimes& times
) {
    const core::SealSatellite& satellite = look.satellites[index];
    if (satellite.layer >= look.layers.size()) {
        return std::nullopt;
    }
    const core::SealLayer& ring = look.layers[satellite.layer];
    const f32 spinScale = frame.looks->spinScale;
    f32 angle = SealRadians(satellite.angle);
    if (satellite.mount == core::SealMount::WithLayer) {
        angle += SealLayerAngle(ring, times.since, spinScale);
    }
    else if (satellite.mount == core::SealMount::Orbit) {
        angle += SealSpin(satellite.orbit, times.since, spinScale);
    }
    const SealPlace place{ .seal = &seal, .plane = look.plane, .lift = look.lift, .viewRatio = frame.looks->viewRatio };
    // It sits across the middle of its ring's band.
    const f32 middle = 0.5F * (ring.inner + ring.outer);
    core::Seal moon = seal;
    moon.key = satellite.seal;
    moon.point = place.At({ .x = middle * std::cos(angle), .y = middle * std::sin(angle) }, ring.height);
    moon.size = seal.size * satellite.size;
    return moon;
}

// A satellite's salt, from its parent's and its place among the parent's satellites, so two alike differ in their light.
u64 SealSatelliteSalt(u64 parent, std::size_t index) {
    return core::detail::Mix(parent ^ core::detail::Mix(index + 1));
}

} // namespace

core::Vec2 SealToWorld(const core::Seal& seal, core::SealPlane plane, f32 lift, f32 viewRatio, SealLocal local) {
    const bool upright = plane == core::SealPlane::Upright;
    // An upright plane's depth runs down it, and its heights come towards the viewer.
    const f32 depth = upright ? local.up : local.depth;
    const f32 rise = (upright ? -local.depth : local.up) + lift;
    return { .x = seal.point.x + local.x, .y = seal.point.y + (depth * viewRatio) - rise };
}

f32 SealLifeOpacity(const core::SealLook& look, std::size_t layer, f64 secondsSinceStart, f64 secondsToEnd) {
    if (layer >= look.layers.size()) {
        return 0.0F;
    }
    f32 appear = secondsSinceStart >= 0.0 ? 1.0F : 0.0F;
    if (look.appear > 0.0) {
        const f64 each = look.appear / static_cast<f64>(look.layers.size());
        const auto turn = static_cast<f64>(SealAppearTurn(look, layer));
        appear = core::CurveAt(look.appearCurve, static_cast<f32>((secondsSinceStart / each) - turn));
    }
    f32 fade = secondsToEnd > 0.0 ? 1.0F : 0.0F;
    if (look.fade > 0.0) {
        // As the glows fade: the curve of the share of the fade that has passed.
        fade = 1.0F - core::CurveAt(look.fadeCurve, static_cast<f32>(1.0 - (secondsToEnd / look.fade)));
    }
    return appear * fade;
}

f32 SealLayerAngle(const core::SealLayer& layer, f64 seconds, f32 spinScale) {
    return SealSpin(layer.spin, seconds, spinScale);
}

void SealDrawing::Clear() {
    groundWash.clear();
    groundLines.clear();
    aboveWash.clear();
    aboveLines.clear();
    layers.clear();
    particles.clear();
    droppedSeals = 0;
}

// The reader's caps bound a pack, so its sizes fit the u32s its GPU buffers take: every look's layers' parts and its
// emblem's, and every glyph set's sprites with every flourish's and emblem's.
constexpr u64 SEAL_MOST_PACKED_PARTS =
    u64{ core::MAX_SEAL_LOOKS } * ((u64{ core::MAX_SEAL_LAYERS } * core::MAX_SEAL_PARTS) + 1);
constexpr u64 SEAL_MOST_PACKED_SPRITES =
    (u64{ core::MAX_SEAL_GLYPH_SETS } * core::MAX_GLYPH_SET_SPRITES) +
    (u64{ core::MAX_SEAL_LOOKS } *
     ((u64{ core::MAX_SEAL_LAYERS } * core::MAX_SEAL_PARTS * std::tuple_size_v<decltype(core::SealPart::sprites)>)+1));
static_assert(SEAL_MOST_PACKED_PARTS * SEAL_GPU_PART_BYTES <= std::numeric_limits<u32>::max(), "the most parts fit a u32");
static_assert(
    SEAL_MOST_PACKED_SPRITES * SEAL_GPU_SPRITE_BYTES <= std::numeric_limits<u32>::max(),
    "the most sprites fit a u32"
);

SealPack PackSeals(const core::Seals& seals, const core::Manifest& manifest) {
    SealPack pack;
    std::vector<u32> glyphSets;
    glyphSets.reserve(seals.glyphSets.size());
    for (const std::vector<core::atlas::SpriteId>& glyphs : seals.glyphSets) {
        glyphSets.push_back(static_cast<u32>(pack.sprites.size()));
        for (const core::atlas::SpriteId glyph : glyphs) {
            pack.sprites.push_back(SealSpriteOf(manifest, glyph));
        }
    }
    pack.looks.reserve(seals.looks.size());
    for (const auto& entry : seals.looks) {
        pack.looks.push_back(SealPackLook(pack, seals, manifest, glyphSets, entry.second));
    }
    // Deepest first, so each depth's counts take the next one's.
    for (u32 depth = core::MAX_SATELLITE_DEPTH + 1; depth-- > 0;) {
        for (std::size_t index = 0; index < seals.looks.size(); ++index) {
            pack.looks[index].seals.at(depth) = SealsDrawnAt(seals, pack, seals.looks[index].second, depth);
        }
    }
    return pack;
}

void DrawSeals(
    std::span<const core::Seal> seals,
    const core::Seals& looks,
    const SealPack& pack,
    const core::UiColours& colours,
    f64 renderSeconds,
    u64 renderTick,
    bool showcase,
    SealDrawing& out
) {
    PN_PROFILE_ZONE();
    SealFrame frame{ .looks = &looks, .pack = &pack, .colours = &colours, .out = &out };
    static_assert(core::MAX_SATELLITE_DEPTH == 2, "satellites are drawn two levels deep");
    for (const core::Seal& seal : seals) {
        if ((seal.showcase && !showcase) || renderTick < seal.startTick || renderTick >= seal.endTick) {
            continue;
        }
        const SealTimes times{ .since = std::max(renderSeconds - (static_cast<f64>(seal.startTick) / SEAL_TICKS_A_SECOND), 0.0),
                               .toEnd = seal.endTick == core::NEVER_ENDS
                                            ? SEAL_FOREVER
                                            : (static_cast<f64>(seal.endTick) / SEAL_TICKS_A_SECOND) - renderSeconds };
        // A seal's point is fixed, so its light's pattern stays put; two seals alike in different places differ.
        const u64 salt =
            core::detail::Mix((u64{ std::bit_cast<u32>(seal.point.x) } << SEAL_HIGH_HALF) | std::bit_cast<u32>(seal.point.y));
        const core::SealLook* look = SealDrawOne(frame, seal, times, salt, 0);
        if (look == nullptr) {
            continue;
        }
        for (std::size_t index = 0; index < look->satellites.size(); ++index) {
            const std::optional<core::Seal> moon = SealSatelliteOf(frame, seal, *look, index, times);
            if (!moon) {
                continue;
            }
            const u64 moonSalt = SealSatelliteSalt(salt, index);
            const core::SealLook* moonLook = SealDrawOne(frame, *moon, times, moonSalt, 1);
            if (moonLook == nullptr) {
                continue;
            }
            for (std::size_t inner = 0; inner < moonLook->satellites.size(); ++inner) {
                if (const std::optional<core::Seal> small = SealSatelliteOf(frame, *moon, *moonLook, inner, times)) {
                    SealDrawOne(frame, *small, times, SealSatelliteSalt(moonSalt, inner), 2);
                }
            }
        }
    }
}

} // namespace gfx
