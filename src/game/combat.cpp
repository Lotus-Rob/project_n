#include "game/combat.hpp"

#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"
#include "game/components.hpp"
#include "game/npcs.hpp"

namespace game {

namespace {

// Slow at both ends, in f64 so an arrival's path is the same bits on every run.
f64 EaseInOut(f64 progress) {
    constexpr f64 THREE = 3.0;
    constexpr f64 TWO = 2.0;
    return progress * progress * (THREE - (TWO * progress));
}

} // namespace

i64 HitAmount(i64 base) {
    return base;
}

i64 HitAmount(const core::AbilityData& /*ability*/, i64 base) {
    return HitAmount(base);
}

f32 DissolveAt(const Dying& dying, u64 tick) {
    // Over the ticks it is drawn, the last of them fully dissolved, so nothing is left to vanish at its removal.
    constexpr u64 DRAWN_STEPS = DYING_TICKS - 1;
    if (tick <= dying.startTick) {
        return 0.0F;
    }
    return static_cast<f32>(std::min(tick - dying.startTick, DRAWN_STEPS)) / static_cast<f32>(DRAWN_STEPS);
}

void Combat::Damage(core::State& state, core::StableId target, i64 amount, u64 tick) {
    if (state.DomainOf(target) != ACTIVE) {
        return;
    }
    core::Registry& entities = state[ACTIVE].Entities();
    const core::Entity entity = state[ACTIVE].Find(target);
    Health* const health = entities.try_get<Health>(entity);
    if (health == nullptr || entities.all_of<Dying>(entity)) {
        return;
    }
    // Each branch stays within i64 whatever the amount: `health->current - health->max` lies in [-max, 0].
    if (amount >= health->current) {
        health->current = 0;
    }
    else if (amount <= health->current - health->max) {
        health->current = health->max;
    }
    else {
        health->current -= amount;
    }
    if (health->current == 0) {
        entities.emplace<Dying>(entity, tick);
        PN_LOG_DEBUG("tick {}: entity {} dies", tick, std::to_underlying(target));
    }
}

u32 Combat::Tick(core::State& state, u64 tick) {
    PN_PROFILE_ZONE();
    core::Registry& entities = state[ACTIVE].Entities();
    std::vector<core::Entity> arrived;
    for (auto [entity, body, arriving] : entities.view<Body, const Arriving>().each()) {
        if (tick >= arriving.startTick + arriving.ticks) {
            arrived.push_back(entity); // where its orbit put it this tick
            continue;
        }
        const f64 progress =
            tick <= arriving.startTick ? 0.0 : static_cast<f64>(tick - arriving.startTick) / static_cast<f64>(arriving.ticks);
        const f64 eased = EaseInOut(progress);
        body.current = {
            .x = static_cast<f32>(
                static_cast<f64>(arriving.from.x) +
                ((static_cast<f64>(body.current.x) - static_cast<f64>(arriving.from.x)) * eased)
            ),
            .y = static_cast<f32>(
                static_cast<f64>(arriving.from.y) +
                ((static_cast<f64>(body.current.y) - static_cast<f64>(arriving.from.y)) * eased)
            ),
        };
    }
    entities.remove<Arriving>(arrived.begin(), arrived.end());

    std::vector<core::StableId> dead;
    for (auto [entity, dying, creature, stableId] : entities.view<const Dying, const Creature, const core::StableId>().each()) {
        if (tick >= dying.startTick + DYING_TICKS) {
            dead.push_back(stableId);
            if (creature.kind == CreatureKind::Fox) {
                m_respawns.push_back(tick + RESPAWN_TICKS);
            }
        }
    }
    for (const core::StableId stableId : dead) {
        state.Destroy(stableId);
    }
    if (!dead.empty()) {
        PN_LOG_DEBUG("tick {}: {} dead creatures removed, {} respawns waiting", tick, dead.size(), m_respawns.size());
    }
    // Queued in tick order, so the due ones lead.
    const auto due = std::ranges::find_if(m_respawns, [tick](u64 dueAt) { return dueAt > tick; });
    const auto count = static_cast<u32>(due - m_respawns.begin());
    m_respawns.erase(m_respawns.begin(), due);
    return count;
}

void Combat::Load(core::Reader& archive) {
    u64 count = 0;
    archive(count);
    if (count > archive.Remaining() / sizeof(u64)) {
        archive.Fail();
    }
    std::vector<u64> respawns;
    respawns.reserve(archive.Failed() ? 0 : static_cast<std::size_t>(count));
    for (u64 i = 0; i < count && !archive.Failed(); ++i) {
        u64 due = 0;
        archive(due);
        if (!respawns.empty() && due < respawns.back()) {
            archive.Fail();
        }
        respawns.push_back(due);
    }
    if (!archive.Failed()) {
        m_respawns = std::move(respawns);
    }
}

} // namespace game
