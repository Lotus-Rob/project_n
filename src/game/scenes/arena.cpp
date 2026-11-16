#include "game/scenes/arena.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/profile.hpp"
#include "core/random.hpp"
#include "core/seal.hpp"
#include "game/combat.hpp"
#include "game/npcs.hpp"
#include "game/random_purposes.hpp"
#include "game/scenes/arena_layout.hpp"

namespace game {

namespace {

constexpr SceneTraits ARENA_TRAITS{
    .name = "arena",
    .view = GROUND,
    .home = ARENA_START,
    .combat = true,
    .creatures = true,
    .demoGlows = ARENA_DEMO_GLOWS,
    .camera = core::CameraKind::Follow,
    .worldNpcs = 5'000,
    .portal = ARENA_PORTAL,
};

constexpr u32 USE_AURA = 1; // the aura's ability use source (core::AbilityUse)

// The showcase seals: between them every part and placement seals.yaml can
// describe. Scene content, as the props are: republished every tick, never saved. Their looks are seals.yaml's; here only
// which seal stands where, and how large (its outer radius). Each has a clearing of its own: none reaches another, nor the
// swords' seal a cast at the player's start raises (app's showcase_tests holds this), so no seal hides another.
struct ShowcaseSeal {
    core::SealKey key = 0;
    core::Vec2 point{};
    f32 size = 0.0F;
};

constexpr std::array SHOWCASE{
    // The largest, about a third of the arena across, in the open lower left below the fight: nothing stands on it.
    ShowcaseSeal{ .key = core::SealKeyOf("showcase_grand"), .point = { .x = 402.0F, .y = 935.0F }, .size = 290.0F },
    // Standing between the dead tree and the crystal.
    ShowcaseSeal{ .key = core::SealKeyOf("showcase_upright"), .point = { .x = 275.0F, .y = 700.0F }, .size = 70.0F },
    // Hung over the open ground between the hut and the shrine, its pillars reaching down into its shadow.
    ShowcaseSeal{ .key = core::SealKeyOf("showcase_overhead"), .point = { .x = 715.0F, .y = 340.0F }, .size = 120.0F },
    // Below the fight, between the tablet and the crane.
    ShowcaseSeal{ .key = core::SealKeyOf("showcase_tiers"), .point = { .x = 975.0F, .y = 890.0F }, .size = 100.0F },
    // In the open ground under the blighted treant, right of the lower right pine's trunk, its shards drifting round it.
    ShowcaseSeal{ .key = core::SealKeyOf("showcase_flame"), .point = { .x = 1650.0F, .y = 968.0F }, .size = 105.0F },
    // The low end, one above the other between the shrine's rock and the fox spirit.
    ShowcaseSeal{ .key = core::SealKeyOf("showcase_small_1"), .point = { .x = 1300.0F, .y = 140.0F }, .size = 40.0F },
    ShowcaseSeal{ .key = core::SealKeyOf("showcase_small_3"), .point = { .x = 1300.0F, .y = 290.0F }, .size = 55.0F },
};

void Aura(SceneContext& context, const core::TickCounters& counters, std::vector<core::Event>& events) {
    PN_PROFILE_ZONE();
    const u64 use = core::AbilityUse(counters.simulation, USE_AURA);
    const std::size_t first = events.size();
    const core::State& state = context.State();
    const core::Registry& entities = state[ACTIVE].Entities();
    const core::CombatRules& rules = context.Data().combat;
    const f32 reach = rules.auraReach;
    for (auto [entity, body, creature, target] : entities.view<const Body, const Creature, const core::StableId>().each()) {
        const core::Vec2 away = body.current - CRYSTAL_FOOT;
        if ((away.x * away.x) + (away.y * away.y) > reach * reach || entities.all_of<Dying>(entity)) {
            continue;
        }
        const bool healing = creature.kind == CreatureKind::Cultivator;
        if (!healing && creature.kind != CreatureKind::Fox) {
            continue;
        }
        const u32 low = healing ? rules.auraHealingMin : rules.auraDamageMin;
        const u32 high = healing ? rules.auraHealingMax : rules.auraDamageMax;
        const u64 random = core::Random(state.Seed(), counters.state, std::to_underlying(target), PURPOSE_AURA, 0);
        events.push_back(
            {
                .kind = core::EventKind::Hit,
                .hit = healing ? core::HitKind::Healing : core::HitKind::Damage,
                .use = use,
                .target = target,
                .position = HeadOf(entities, context.Manifest(), entity),
                .value = HitAmount(static_cast<i64>(low) + static_cast<i64>(core::ToBelow(random, high - low + 1))),
            }
        );
    }
    // Applied once every hit is chosen: a death changes the components the loop above reads.
    for (const core::Event& hit : std::span{ events }.subspan(first)) {
        context.Damage(hit.target, hit.hit == core::HitKind::Healing ? -hit.value : hit.value, counters.simulation);
    }
    events.push_back({ .kind = core::EventKind::UseEnded, .use = use });
}

} // namespace

const SceneTraits& ArenaScene::Traits() const {
    return ARENA_TRAITS;
}

void ArenaScene::Build(SceneContext& context) const {
    BuildArenaLayout(context);
    SpawnArenaNpcs(context);
}

void ArenaScene::Tick(SceneContext& context, const core::TickCounters& counters, std::vector<core::Event>& events) const {
    PN_PROFILE_ZONE();
    if (counters.simulation % context.Data().combat.auraPeriod == 0) {
        Aura(context, counters, events);
    }
}

void ArenaScene::Respawn(SceneContext& context, const core::TickCounters& counters, u32 index) const {
    RespawnFox(context, counters, index);
}

void ArenaScene::Publish(const SceneContext& context, core::Rect /*view*/, core::Snapshot& snapshot) const {
    PN_PROFILE_ZONE();
    PublishCaption(context, snapshot);
    for (const ShowcaseSeal& seal : SHOWCASE) {
        snapshot.seals.push_back(
            { .key = seal.key, .point = seal.point, .size = seal.size, .endTick = core::NEVER_ENDS, .showcase = true }
        );
    }
}

} // namespace game
