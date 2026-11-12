#include "gfx/volley_draw.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/profile.hpp"
#include "core/tick_clock.hpp"

namespace gfx {

namespace {

// Swords a job chunk draws: enough work to be worth a worker's wake, few enough that a volley of 1,000 spreads.
constexpr u32 SWORD_CHUNK = 256;
// How much larger than its image a sword is drawn: 1,000 at full size cover their ring several times over and read as one
// grey mass; at this size they read as swords in a ring.
constexpr f32 SWORD_SCALE = 0.6F;
// How much larger than its image a halo is drawn: a glow round the blade a little longer than it.
constexpr f32 HALO_SCALE = 0.75F;
// How much larger than its image (16 world units across) a sword's tinted core is drawn: under the middle of the blade,
// smaller than its halo. The bolt's is twice as large, as the bolt is.
constexpr f32 CORE_SCALE = 1.0F;
constexpr f32 BOLT_CORE_SCALE = 2.0F;
// A trail's width at the sword, in world units: about the drawn blade's.
constexpr f32 TRAIL_WIDTH = 5.0F;
// How much of that width its tail loses, so it narrows as it fades.
constexpr f32 TRAIL_TAPER = 0.6F;
// A tangent shorter than this, in world units, is too short to say which way the trail runs there.
constexpr f32 SHORTEST_TANGENT = 1e-4F;
constexpr u32 TRAIL_POINTS = TRAIL_SEGMENTS + 1;
// A bar's middle, down from its top as a fraction of its height: where the solid square's foot goes.
constexpr f32 BAR_MIDDLE = 0.5F;

f32 Length(core::Vec2 vec) {
    return std::hypot(vec.x, vec.y);
}

// The clockwise rotation that turns a sprite pointing up (towards -y, as world y grows downwards) to `heading`.
f32 RotationTo(core::Vec2 heading) {
    return std::atan2(heading.x, -heading.y);
}

// What every sword of a frame shares.
struct SwordFrame {
    const core::Manifest* manifest = nullptr;
    const core::UiColours* colours = nullptr;
    u64 simulationTick = 0;
    f32 alpha = 0.0F;
    f64 renderTick = 0.0;
};

// Where one sword's core, halo, sprite and trail go.
struct SwordSlots {
    core::Instance* core = nullptr;
    core::Instance* halo = nullptr;
    core::Instance* sword = nullptr;
    std::span<RibbonVertex> trail;
};

// `sword`'s trail: TRAIL_POINTS points on its path, from `headTick` back to `tailTick`, flying at `aim`, `head` the first.
void DrawTrail(
    const core::Volley& volley,
    u32 sword,
    core::Vec2 aim,
    f64 headTick,
    f64 tailTick,
    const core::SwordPose& head,
    u32 colour,
    std::span<RibbonVertex> trail
) {
    std::array<core::Vec2, TRAIL_POINTS> points{};
    points[0] = head.position;
    for (u32 point = 1; point < TRAIL_POINTS; ++point) {
        const f64 along = static_cast<f64>(point) / static_cast<f64>(TRAIL_SEGMENTS);
        points.at(point) = core::SwordAt(volley, sword, headTick - ((headTick - tailTick) * along), aim).position;
    }
    std::array<core::Vec2, TRAIL_POINTS> lefts{};
    std::array<core::Vec2, TRAIL_POINTS> rights{};
    for (u32 point = 0; point < TRAIL_POINTS; ++point) {
        // Across the way the trail runs there, from its neighbours, so the ribbon bends with the path without twisting.
        const core::Vec2 runs = points.at(point == 0 ? 0 : point - 1) - points.at(std::min(point + 1, TRAIL_SEGMENTS));
        const f32 length = Length(runs);
        const core::Vec2 along = length > SHORTEST_TANGENT ? runs * (1.0F / length) : head.heading;
        const f32 back = static_cast<f32>(point) / static_cast<f32>(TRAIL_SEGMENTS);
        const core::Vec2 side =
            core::Vec2{ .x = -along.y, .y = along.x } * (TRAIL_WIDTH * (1.0F - (TRAIL_TAPER * back)) * 0.5F);
        lefts.at(point) = points.at(point) + side;
        rights.at(point) = points.at(point) - side;
    }
    const auto vertex = [colour](core::Vec2 position, f32 back, f32 across) {
        return RibbonVertex{ .x = position.x,
                             .y = position.y,
                             .uv = core::PackPair(core::PackUnorm16(back), core::PackUnorm16(across)),
                             .colour = colour };
    };
    for (u32 quad = 0; quad < TRAIL_SEGMENTS; ++quad) {
        const f32 near = static_cast<f32>(quad) / static_cast<f32>(TRAIL_SEGMENTS);
        const f32 far = static_cast<f32>(quad + 1) / static_cast<f32>(TRAIL_SEGMENTS);
        const std::span<RibbonVertex> corners = trail.subspan(std::size_t{ quad } * QUAD_VERTICES, QUAD_VERTICES);
        corners[0] = vertex(lefts.at(quad), near, 0.0F);
        corners[1] = vertex(rights.at(quad), near, 1.0F);
        corners[2] = vertex(lefts.at(quad + 1), far, 0.0F);
        corners[3] = vertex(rights.at(quad + 1), far, 1.0F);
    }
}

// Fills `sword`'s slots: its halo and sprite where the path puts it at the frame's render time, and its trail; zeros
// where nothing shows.
void DrawSword(
    const SwordFrame& frame,
    const core::VolleyView& view,
    std::span<const core::VolleyTarget> targets,
    u32 sword,
    const SwordSlots& slots
) {
    const core::Volley& volley = view.volley;
    *slots.core = {};
    *slots.halo = {};
    *slots.sword = {};
    std::ranges::fill(slots.trail, RibbonVertex{});
    const auto launch = static_cast<f64>(core::LaunchTick(volley, sword));
    if (frame.renderTick < launch) {
        return;
    }
    const u64 arrival = core::ArrivalTick(volley, sword);
    const core::Vec2 aim = core::SwordAim(volley, sword, frame.simulationTick, frame.alpha, targets);
    // Struck: shown on its target at the moment of its arrival, gone after, its trail draining into the target.
    const bool struck = frame.renderTick >= static_cast<f64>(arrival) &&
                        targets[core::SwordTarget(volley, sword, arrival, targets)].Struck(arrival);
    const f64 headTick = struck ? static_cast<f64>(arrival) : frame.renderTick;
    const core::SwordPose head = core::SwordAt(volley, sword, headTick, aim);
    const u32 effect = frame.colours->Of(core::EffectColour(view.element));
    const bool gone = struck && frame.renderTick > static_cast<f64>(arrival);
    if (!gone && head.shown > 0.0F) {
        const f32 rotation = RotationTo(head.heading);
        const bool bolt = view.ability == core::AbilityKind::FireBolt;
        *slots.core = core::MakeInstance(
            *frame.manifest,
            { .sprite = core::atlas::SpriteId::GlowCore,
              .previous = head.position,
              .current = head.position,
              .scale = bolt ? BOLT_CORE_SCALE : CORE_SCALE,
              .colour = core::ScaledColour(effect, CORE_OPACITY * head.shown) }
        );
        // The glow goes before the blade, as the square of what shows: a plunged sword doesn't flash as it fades.
        *slots.halo = core::MakeInstance(
            *frame.manifest,
            { .sprite = core::atlas::SpriteId::Halo,
              .previous = head.position,
              .current = head.position,
              .scale = HALO_SCALE,
              .colour = core::ScaledColour(effect, HALO_INTENSITY * head.shown * head.shown) }
        );
        *slots.sword = core::MakeInstance(
            *frame.manifest,
            { .sprite = bolt ? core::atlas::SpriteId::FireBolt : core::atlas::SpriteId::Sword,
              .previous = head.position,
              .current = head.position,
              .scale = bolt ? 1.0F : SWORD_SCALE,
              .previousRotation = rotation,
              .currentRotation = rotation,
              .colour = core::ScaledColour(bolt ? effect : core::OPAQUE_WHITE, head.shown) }
        );
    }
    // A sword still on its ring has not moved, so its trail starts where it left.
    const auto departure = static_cast<f64>(core::DepartureTick(volley, sword));
    const f64 tailTick = std::max(frame.renderTick - static_cast<f64>(TRAIL_TICKS), departure);
    if (headTick > tailTick && head.shown > 0.0F) {
        DrawTrail(
            volley, sword, aim, headTick, tailTick, head, core::ScaledColour(effect, HALO_INTENSITY * head.shown), slots.trail
        );
    }
}

} // namespace

void VolleyDrawing::Clear() {
    instances.clear();
    batches.clear();
    ribbons.clear();
    hud.clear();
    droppedSwords = 0;
}

f32 BarOpacity(const PlacedLabel& label) {
    // The share of the cross-fade showing the name alone: its new count's, its old count's, or both.
    const f32 alone = (label.count == 0 ? label.change : 0.0F) + (label.previous == 0 ? 1.0F - label.change : 0.0F);
    return label.opacity * alone;
}

void DrawHealthBars(
    std::span<const core::HealthBar> bars,
    std::span<const BarShown> shown,
    f32 alpha,
    const core::Manifest& manifest,
    const core::UiColours& colours,
    VolleyDrawing& out
) {
    PN_PROFILE_ZONE();
    if (bars.empty()) {
        return;
    }
    const core::SpriteInfo& solid = manifest.Sprite(core::atlas::SpriteId::Solid);
    const auto width = static_cast<f32>(solid.width);
    const auto height = static_cast<f32>(solid.height);
    const u32 backColour = colours.Of(core::UiColour::BarBackground);
    const u32 fillColour = colours.Of(core::UiColour::EnemyHealthBar);
    const auto first = static_cast<u32>(out.instances.size());
    for (const core::HealthBar& bar : bars) {
        const auto label = std::ranges::lower_bound(shown, bar.entity, {}, &BarShown::entity);
        const f32 opacity = label != shown.end() && label->entity == bar.entity ? label->opacity : 0.0F;
        if (!(opacity > 0.0F)) {
            continue;
        }
        const core::Vec2 head = bar.previous + ((bar.current - bar.previous) * alpha);
        const core::Vec2 middle{ .x = head.x, .y = head.y - HEALTH_BAR_RISE };
        // The square's foot is its left middle: a bar `full` long at most stands on its left end, `length` long.
        const auto add = [&](f32 full, f32 length, f32 thickness, u32 colour) {
            const core::Vec2 foot{ .x = middle.x - (full * 0.5F), .y = middle.y };
            const f32 scale = thickness / height;
            out.instances.push_back(
                core::MakeInstance(
                    manifest,
                    { .sprite = core::atlas::SpriteId::Solid,
                      .previous = foot,
                      .current = foot,
                      .scale = scale,
                      .colour = colour,
                      .widthScale = length / (width * scale) }
                )
            );
        };
        add(HEALTH_BAR_WIDTH, HEALTH_BAR_WIDTH, HEALTH_BAR_HEIGHT, core::ScaledColour(backColour, std::min(opacity, 1.0F)));
        // The fill starts where a full one would, so it shrinks towards the left as health falls.
        const f32 fraction = bar.fraction > 0.0F ? std::min(bar.fraction, 1.0F) : 0.0F;
        add(HEALTH_FILL_WIDTH,
            HEALTH_FILL_WIDTH * fraction,
            HEALTH_FILL_HEIGHT,
            core::ScaledColour(fillColour, std::min(opacity, 1.0F)));
    }
    if (out.instances.size() > first) {
        out.batches.push_back(
            { .first = first, .count = static_cast<u32>(out.instances.size()) - first, .pipeline = core::Pipeline::Normal }
        );
    }
}

void DrawHudBars(std::span<const HudBar> bars, const core::Manifest& manifest, VolleyDrawing& out) {
    PN_PROFILE_ZONE();
    const core::SpriteInfo& solid = manifest.Sprite(core::atlas::SpriteId::Solid);
    const auto width = static_cast<f32>(solid.width);
    const auto height = static_cast<f32>(solid.height);
    const auto first = static_cast<u32>(out.instances.size());
    for (const HudBar& bar : bars) {
        const core::Vec2 foot{ .x = bar.topLeft.x, .y = bar.topLeft.y + (bar.size.y * BAR_MIDDLE) };
        const f32 scale = bar.size.y / height;
        const auto add = [&](f32 length, u32 colour) {
            out.instances.push_back(
                core::MakeInstance(
                    manifest,
                    { .sprite = core::atlas::SpriteId::Solid,
                      .previous = foot,
                      .current = foot,
                      .scale = scale,
                      .colour = colour,
                      .widthScale = length / (width * scale) }
                )
            );
        };
        add(bar.size.x, bar.background);
        const f32 fraction = bar.fraction > 0.0F ? std::min(bar.fraction, 1.0F) : 0.0F;
        if (fraction > 0.0F) {
            add(bar.size.x * fraction, bar.fill);
        }
    }
    if (out.instances.size() > first) {
        out.hud.push_back(
            { .first = first, .count = static_cast<u32>(out.instances.size()) - first, .pipeline = core::Pipeline::Normal }
        );
    }
}

void DrawFlashes(
    std::span<const core::Flash> flashes,
    f64 renderTick,
    const core::Manifest& manifest,
    const core::UiColours& colours,
    VolleyDrawing& out
) {
    PN_PROFILE_ZONE();
    const auto first = static_cast<u32>(out.instances.size());
    for (const core::Flash& flash : flashes) {
        const f64 progress = (renderTick - static_cast<f64>(flash.startTick)) / static_cast<f64>(core::FLASH_TICKS);
        // False for a NaN render tick too, so nothing is drawn then.
        const bool showing = progress >= 0.0 && progress < 1.0;
        if (!showing) {
            continue;
        }
        const auto shown = static_cast<f32>(progress);
        const f32 grown = 1.0F - ((1.0F - shown) * (1.0F - shown)); // fast at first, slowing to its full size
        out.instances.push_back(
            core::MakeInstance(
                manifest,
                { .sprite = core::atlas::SpriteId::LanceStrike,
                  .previous = flash.point,
                  .current = flash.point,
                  .scale = FLASH_START_SCALE + ((FLASH_END_SCALE - FLASH_START_SCALE) * grown),
                  .colour =
                      core::ScaledColour(colours.Of(core::EffectColour(flash.element)), FLASH_INTENSITY * (1.0F - shown)) }
            )
        );
    }
    if (out.instances.size() > first) {
        out.batches.push_back(
            { .first = first, .count = static_cast<u32>(out.instances.size()) - first, .pipeline = core::Pipeline::Additive }
        );
    }
}

void DrawVolleys(
    std::span<const core::VolleyView> volleys,
    std::span<const core::VolleyTarget> targets,
    u64 simulationTick,
    f32 alpha,
    const core::Manifest& manifest,
    const core::UiColours& colours,
    core::Jobs& jobs,
    VolleyDrawing& out
) {
    PN_PROFILE_ZONE();
    std::size_t all = 0;
    for (const core::VolleyView& view : volleys) {
        all += view.volley.shape.count;
    }
    // Whole swords only: those past the overlay's room or the trails' are left out, core, halo, blade and trail.
    const auto total = static_cast<u32>(std::min(all, SwordRoom(out.instances.size(), out.ribbons.size())));
    out.droppedSwords += all - total;
    if (total == 0) {
        return;
    }
    const auto cores = static_cast<u32>(out.instances.size());
    const u32 halos = cores + total;
    const u32 swords = halos + total;
    out.instances.resize(std::size_t{ swords } + total);
    const std::size_t firstVertex = out.ribbons.size();
    out.ribbons.resize(firstVertex + (std::size_t{ total } * TRAIL_VERTICES));
    // The cores and halos under everything already there (the health bars, which the halos would wash out); the swords
    // over all.
    const std::array<core::Batch, 2> under{ {
        { .first = cores, .count = total, .pipeline = core::Pipeline::Normal },
        { .first = halos, .count = total, .pipeline = core::Pipeline::Additive },
    } };
    out.batches.insert(out.batches.begin(), under.begin(), under.end());
    out.batches.push_back({ .first = swords, .count = total, .pipeline = core::Pipeline::Normal });
    const SwordFrame frame{ .manifest = &manifest,
                            .colours = &colours,
                            .simulationTick = simulationTick,
                            .alpha = alpha,
                            .renderTick = core::RenderTick(simulationTick, static_cast<f64>(alpha)) };
    const std::span<core::Instance> instances{ out.instances };
    const std::span<RibbonVertex> ribbons = std::span{ out.ribbons }.subspan(firstVertex);
    // Each chunk writes only its own swords' slots, so the bytes are the same whoever runs it.
    const auto body = [&](u32 chunk) {
        const u32 begin = chunk * SWORD_CHUNK;
        const u32 end = std::min(begin + SWORD_CHUNK, total);
        u32 volley = 0;
        u32 volleyFirst = 0; // the index of its first sword across the volleys
        for (u32 index = begin; index < end; ++index) {
            while (index >= volleyFirst + volleys[volley].volley.shape.count) {
                volleyFirst += volleys[volley].volley.shape.count;
                ++volley;
            }
            const core::VolleyView& view = volleys[volley];
            DrawSword(
                frame,
                view,
                targets.subspan(view.firstTarget, view.targets),
                index - volleyFirst,
                { .core = &instances[cores + index],
                  .halo = &instances[halos + index],
                  .sword = &instances[swords + index],
                  .trail = ribbons.subspan(std::size_t{ index } * TRAIL_VERTICES, TRAIL_VERTICES) }
            );
        }
    };
    const u32 chunks = core::ChunkCount(total, SWORD_CHUNK);
    if (chunks == 1) {
        body(0U); // sparing a single bolt the wake of every worker
    }
    else {
        jobs.ParallelFor(chunks, body);
    }
}

} // namespace gfx
