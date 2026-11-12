#include "game/combat.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/log.hpp"
#include "core/profile.hpp"
#include "core/random.hpp"
#include "game/components.hpp"
#include "game/npcs.hpp"
#include "game/random_purposes.hpp"

namespace game {

namespace {

// Slow at both ends, in f64 so an arrival's path is the same bits on every run.
f64 EaseInOut(f64 progress) {
    constexpr f64 THREE = 3.0;
    constexpr f64 TWO = 2.0;
    return progress * progress * (THREE - (TWO * progress));
}

void StartDash(core::Registry& entities, core::Entity caster, const core::AbilityData& ability, core::Vec2 point, u64 tick) {
    const core::Vec2 from = entities.get<Body>(caster).current;
    const f64 toX = static_cast<f64>(point.x) - static_cast<f64>(from.x);
    const f64 toY = static_cast<f64>(point.y) - static_cast<f64>(from.y);
    const f64 away = std::sqrt((toX * toX) + (toY * toY));
    const f64 reach = std::min(away, static_cast<f64>(ability.distance));
    const f64 scale = away > 0.0 ? reach / away : 0.0;
    const core::Vec2 end{ .x = static_cast<f32>(static_cast<f64>(from.x) + (toX * scale)),
                          .y = static_cast<f32>(static_cast<f64>(from.y) + (toY * scale)) };
    entities.emplace<Dashing>(caster, from, end, tick, std::max(ability.durationTicks, 1U));
    PN_LOG_DEBUG("tick {}: the player dashes from ({}, {}) to ({}, {})", tick, from.x, from.y, end.x, end.y);
}

f64 SquaredDistance(core::Vec2 start, core::Vec2 end) {
    const f64 across = static_cast<f64>(end.x) - static_cast<f64>(start.x);
    const f64 down = static_cast<f64>(end.y) - static_cast<f64>(start.y);
    return (across * across) + (down * down);
}

// How far above its foot `entity`'s sprite reaches, in world units.
f32 HeightOf(const core::Registry& entities, const core::Manifest& manifest, core::Entity entity) {
    const Look& look = entities.get<Look>(entity);
    return static_cast<f32>(manifest.Sprite(look.sprite).footY) * look.scale;
}

core::Vec2 Raised(core::Vec2 foot, f32 height) {
    return { .x = foot.x, .y = foot.y - height };
}

// Where a hit on `entity` shows: its head.
core::Vec2 HeadOf(const core::Registry& entities, const core::Manifest& manifest, core::Entity entity) {
    return Raised(entities.get<Body>(entity).current, HeightOf(entities, manifest, entity));
}

// Where the volleys aim at `entity`, its chest, at the previous tick and this one.
struct Chest {
    core::Vec2 previous{};
    core::Vec2 current{};
};

Chest ChestOf(const core::Registry& entities, const core::Manifest& manifest, core::Entity entity) {
    const f32 half = HeightOf(entities, manifest, entity) / 2.0F;
    const Body& body = entities.get<Body>(entity);
    return { .previous = Raised(body.previous, half), .current = Raised(body.current, half) };
}

// An enemy a cast tagged: the foxes are the only enemies so far.
struct Tagged {
    core::StableId entity{};
    core::Vec2 chest{};
    f64 away = 0.0; // squared, from the point
};

// Every living enemy whose chest is within `radius` of `point`, in stable ID order.
std::vector<Tagged> Tag(const core::Registry& entities, const core::Manifest& manifest, core::Vec2 point, f32 radius) {
    const f64 reach = static_cast<f64>(radius) * static_cast<f64>(radius);
    std::vector<Tagged> tagged;
    for (const core::Entity entity : entities.view<const Creature, const Health, const core::StableId>()) {
        if (entities.get<Creature>(entity).kind != CreatureKind::Fox || entities.all_of<Dying>(entity)) {
            continue;
        }
        const core::Vec2 chest = ChestOf(entities, manifest, entity).current;
        const f64 away = SquaredDistance(point, chest);
        if (away <= reach) {
            tagged.push_back({ .entity = entities.get<core::StableId>(entity), .chest = chest, .away = away });
        }
    }
    std::ranges::sort(tagged, {}, &Tagged::entity);
    return tagged;
}

// The ticks a bolt at `speed` world units a tick takes from `start` to `end`, ⌈distance / speed⌉: none at no speed.
u32 FlightTicks(core::Vec2 start, core::Vec2 end, f32 speed) {
    if (!(speed > 0.0F)) {
        return 0;
    }
    const f64 ticks = std::ceil(std::sqrt(SquaredDistance(start, end)) / static_cast<f64>(speed));
    constexpr auto MOST = static_cast<f64>(std::numeric_limits<u32>::max());
    return ticks < MOST ? static_cast<u32>(ticks) : std::numeric_limits<u32>::max();
}

// The affliction a hit of `element` leaves: a poison's ticks are Poison hits, any other's a burn's.
AfflictionState& AfflictionOf(Afflictions& afflictions, core::HitKind element) {
    return element == core::HitKind::Poison ? afflictions.poison : afflictions.burn;
}

// Leaves `affliction` (its damage as cast) of `element` on `entity` for `use`, its first tick AFFLICTION_PERIOD after
// `tick`: refreshing one there, whose use is added to `ending` if it was another's. Nothing for an affliction of no ticks.
void Afflict(
    core::Registry& entities,
    core::Entity entity,
    core::HitKind element,
    const core::Affliction& affliction,
    u64 use,
    u64 tick,
    std::vector<u64>& ending
) {
    if (affliction.ticks == 0) {
        return;
    }
    AfflictionState& state = AfflictionOf(entities.get_or_emplace<Afflictions>(entity), element);
    if (state.ticksLeft > 0 && state.use != use) {
        ending.push_back(state.use); // refreshed: its ticks are the new use's now
    }
    state = { .damage = affliction.damage, .ticksLeft = affliction.ticks, .use = use, .nextTick = tick + AFFLICTION_PERIOD };
}

// Deals each affliction due at `tick` on a living creature: a hit of its element and use at the creature's head. An
// affliction's last tick adds its use to `ending`.
void TickAfflictions(
    core::State& state,
    const core::Manifest& manifest,
    u64 tick,
    std::vector<core::Event>& events,
    std::vector<u64>& ending
) {
    core::Registry& entities = state[ACTIVE].Entities();
    // Copied first: a tick's damage may make the creature Dying, adding to the pools the view is built on.
    const auto view = entities.view<const Afflictions>();
    const std::vector<core::Entity> afflicted(view.begin(), view.end());
    for (const core::Entity entity : afflicted) {
        if (entities.all_of<Dying>(entity)) {
            continue;
        }
        for (const core::HitKind element : { core::HitKind::Burn, core::HitKind::Poison }) {
            AfflictionState& affliction = AfflictionOf(entities.get<Afflictions>(entity), element);
            if (affliction.ticksLeft == 0 || affliction.nextTick > tick) {
                continue;
            }
            const core::StableId target = entities.get<core::StableId>(entity);
            events.push_back(
                {
                    .kind = core::EventKind::Hit,
                    .hit = element,
                    .use = affliction.use,
                    .target = target,
                    .position = HeadOf(entities, manifest, entity),
                    .value = affliction.damage,
                }
            );
            --affliction.ticksLeft;
            affliction.nextTick += AFFLICTION_PERIOD;
            if (affliction.ticksLeft == 0) {
                ending.push_back(affliction.use);
            }
            Combat::Damage(state, target, affliction.damage, tick);
        }
    }
}

// Ends the afflictions of every dying creature, adding their uses to `ending`, and removes the spent ones.
void EndDyingAfflictions(core::Registry& entities, std::vector<u64>& ending) {
    std::vector<core::Entity> cured;
    for (auto [entity, afflictions] : entities.view<Afflictions>().each()) {
        if (entities.all_of<Dying>(entity)) {
            for (AfflictionState* const affliction : { &afflictions.burn, &afflictions.poison }) {
                if (affliction->ticksLeft > 0) {
                    ending.push_back(affliction->use);
                    affliction->ticksLeft = 0;
                }
            }
        }
        if (afflictions.burn.ticksLeft == 0 && afflictions.poison.ticksLeft == 0) {
            cured.push_back(entity);
        }
    }
    entities.remove<Afflictions>(cured.begin(), cured.end());
}

} // namespace

void Combat::Cast(
    core::State& state,
    const core::Manifest& manifest,
    const core::Abilities& abilities,
    core::StableId player,
    const core::Action& cast,
    const core::TickCounters& counters,
    std::vector<core::Event>& events
) {
    const u64 tick = counters.simulation;
    if (state.DomainOf(player) != ACTIVE || !core::IsFinite(cast.point)) {
        return;
    }
    core::Registry& entities = state[ACTIVE].Entities();
    const core::Entity entity = state[ACTIVE].Find(player);
    Caster* const caster = entities.try_get<Caster>(entity);
    if (caster == nullptr || entities.all_of<Dying>(entity)) {
        return;
    }
    if (entities.all_of<Dashing>(entity)) {
        PN_LOG_DEBUG("tick {}: a cast in slot {} is ignored: the player is dashing", tick, std::to_underlying(cast.slot));
        return;
    }
    const auto slot = std::to_underlying(cast.slot);
    if (slot >= core::SLOTS) {
        return;
    }
    const core::AbilityKind kind = abilities.In(cast.slot);
    if (kind == core::AbilityKind::None || tick < caster->readyTick.at(slot)) {
        return;
    }
    const core::AbilityData& ability = abilities.Of(kind);
    if (caster->qi < ability.qiCost) {
        events.push_back({ .kind = core::EventKind::NotEnoughQi, .value = static_cast<i64>(slot) });
        PN_LOG_DEBUG("tick {}: slot {} needs {} Qi, the player has {}", tick, slot, ability.qiCost, caster->qi);
        return;
    }
    // The gain is capped before the cost is taken, so a cast never loses Qi to the cap.
    const i64 room = std::max<i64>(0, abilities.qiMax - caster->qi);
    caster->qi += std::min(ability.qiGain, room) - ability.qiCost;
    caster->readyTick.at(slot) = tick + ability.cooldownTicks;
    Start(state, manifest, entity, ability, kind, cast, counters);
}

void Combat::Start(
    core::State& state,
    const core::Manifest& manifest,
    core::Entity caster,
    const core::AbilityData& ability,
    core::AbilityKind kind,
    const core::Action& cast,
    const core::TickCounters& counters
) {
    if (kind == core::AbilityKind::Dash) {
        StartDash(state[ACTIVE].Entities(), caster, ability, cast.point, counters.simulation);
    }
    else if (kind == core::AbilityKind::FireBolt || kind == core::AbilityKind::FlyingSwords) {
        Launch(state, manifest, caster, ability, kind, cast, counters);
    }
    else if (kind == core::AbilityKind::PoisonLance) {
        Lance(ability, cast, counters.simulation);
    }
}

void Combat::Lance(const core::AbilityData& ability, const core::Action& cast, u64 tick) {
    m_strikes.push_back(
        {
            .use = CastUse(tick, cast.slot),
            .point = cast.point,
            .radius = ability.radius,
            .element = ability.hit,
            .damage = HitAmount(ability, ability.damage),
            .affliction = { .ticks = ability.affliction.ticks, .damage = HitAmount(ability, ability.affliction.damage) },
        }
    );
}

void Combat::DealStrike(
    core::State& state,
    const core::Manifest& manifest,
    const LanceStrike& strike,
    u64 tick,
    std::vector<core::Event>& events,
    std::vector<u64>& ending
) {
    core::Registry& entities = state[ACTIVE].Entities();
    // The lance's own use ends this tick unless an affliction it leaves holds it.
    ending.push_back(strike.use);
    const std::vector<Tagged> tagged = Tag(entities, manifest, strike.point, strike.radius);
    for (const Tagged& enemy : tagged) {
        const core::Entity entity = state[ACTIVE].Find(enemy.entity);
        events.push_back(
            {
                .kind = core::EventKind::Hit,
                .hit = strike.element,
                .use = strike.use,
                .target = enemy.entity,
                .position = HeadOf(entities, manifest, entity),
                .value = strike.damage,
            }
        );
        Damage(state, enemy.entity, strike.damage, tick);
        if (!entities.all_of<Dying>(entity)) {
            Afflict(entities, entity, strike.element, strike.affliction, strike.use, tick, ending);
        }
    }
    m_flashes.push_back({ .point = strike.point, .startTick = tick, .element = strike.element });
    PN_LOG_DEBUG(
        "tick {}: the poison lance strikes at ({}, {}): {} enemies hit", tick, strike.point.x, strike.point.y, tagged.size()
    );
}

void Combat::Launch(
    core::State& state,
    const core::Manifest& manifest,
    core::Entity caster,
    const core::AbilityData& ability,
    core::AbilityKind kind,
    const core::Action& cast,
    const core::TickCounters& counters
) {
    const core::Registry& entities = state[ACTIVE].Entities();
    const u64 tick = counters.simulation;
    std::vector<Tagged> tagged = Tag(entities, manifest, cast.point, ability.radius);
    const core::Vec2 origin = ChestOf(entities, manifest, caster).current;
    core::VolleyShape shape = ability.volley;
    shape.commitTicks = core::CommitTicksFor(ability.radius);
    if (kind == core::AbilityKind::FireBolt) {
        if (!tagged.empty()) {
            // Ties go to the first in stable ID order, as min_element keeps the first of equals.
            const Tagged nearest = *std::ranges::min_element(tagged, {}, &Tagged::away);
            tagged = { nearest };
        }
        shape.count = 1;
        shape.waves = 1;
        shape.ringRadius = 0.0F;
        shape.ringStep = 0.0F;
        shape.scatter = 0.0F;
        shape.angleJitter = 0.0F;
        shape.staggerTicks = 0;
        shape.waveGapTicks = 0;
        shape.outTicks = 0;
        shape.holdTicks = 0;
        shape.departJitterTicks = 0;
        shape.sweep = 0.0F;
        shape.sweepJitter = 0.0F;
        shape.flightTicks = FlightTicks(origin, tagged.empty() ? cast.point : tagged.front().chest, ability.speed);
    }
    Flight flight{
        .volley = { .use = CastUse(tick, cast.slot),
                    .startTick = tick,
                    .seed = core::Random(
                        state.Seed(),
                        counters.state,
                        std::to_underlying(entities.get<core::StableId>(caster)),
                        PURPOSE_VOLLEY,
                        std::to_underlying(cast.slot)
                    ),
                    .origin = origin,
                    .point = cast.point,
                    .shape = shape },
        .ability = kind,
        .element = ability.hit,
        .damage = HitAmount(ability, ability.damage),
        .affliction = { .ticks = ability.affliction.ticks, .damage = HitAmount(ability, ability.affliction.damage) },
    };
    for (const Tagged& enemy : tagged) {
        flight.targets.push_back({ .entity = enemy.entity, .previous = enemy.chest, .current = enemy.chest });
    }
    if (flight.targets.empty()) {
        // Dead from the cast, so every sword plunges there. Simulation ticks count from 1, as `diedTick` 0 means alive.
        flight.targets.push_back(
            { .entity = {}, .previous = cast.point, .current = cast.point, .diedTick = std::max<u64>(tick, 1) }
        );
    }
    PN_ASSERT(flight.volley.Valid(), "a cast resolved an invalid volley");
    flight.Prepare();
    PN_LOG_DEBUG(
        "tick {}: {} cast at ({}, {}): {} enemies tagged, {} launched, the last arriving at tick {}",
        tick,
        core::ABILITY_NAMES.at(std::to_underlying(kind)),
        cast.point.x,
        cast.point.y,
        tagged.size(),
        shape.count,
        flight.lastArrival
    );
    m_flights.push_back(std::move(flight));
}

bool Combat::Dash(core::State& state, core::StableId player, u64 tick) {
    if (state.DomainOf(player) != ACTIVE) {
        return false;
    }
    core::Registry& entities = state[ACTIVE].Entities();
    const core::Entity entity = state[ACTIVE].Find(player);
    const Dashing* const dashing = entities.try_get<Dashing>(entity);
    if (dashing == nullptr) {
        return false;
    }
    Body& body = entities.get<Body>(entity);
    body.previous = body.current;
    // The cast's own tick takes the first step, so a dash has no tick of lag: it ends on its tick `ticks - 1` after the cast.
    const u64 step = tick + 1 - std::min(tick + 1, dashing->startTick);
    if (step >= dashing->ticks) {
        body.current = dashing->to;
        entities.remove<Dashing>(entity);
        return true;
    }
    const f64 eased = EaseInOut(static_cast<f64>(step) / static_cast<f64>(dashing->ticks));
    body.current = {
        .x = static_cast<f32>(
            static_cast<f64>(dashing->from.x) + ((static_cast<f64>(dashing->to.x) - static_cast<f64>(dashing->from.x)) * eased)
        ),
        .y = static_cast<f32>(
            static_cast<f64>(dashing->from.y) + ((static_cast<f64>(dashing->to.y) - static_cast<f64>(dashing->from.y)) * eased)
        ),
    };
    return true;
}

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

void Combat::Flight::Prepare() {
    const u32 count = volley.shape.count;
    std::vector<std::pair<u64, u32>> arriving;
    arriving.reserve(count);
    for (u32 sword = 0; sword < count; ++sword) {
        arriving.emplace_back(core::ArrivalTick(volley, sword), sword);
    }
    std::ranges::sort(arriving); // swords landing on one tick land in sword order
    order.clear();
    arrivals.clear();
    order.reserve(count);
    arrivals.reserve(count);
    for (const auto& [arrival, sword] : arriving) {
        arrivals.push_back(arrival);
        order.push_back(sword);
    }
    lastArrival = arrivals.empty() ? volley.startTick : arrivals.back();
}

void Combat::Arrive(
    core::State& state,
    const core::Manifest& manifest,
    const Flight& flight,
    u32 sword,
    u64 tick,
    std::vector<core::Event>& events,
    std::vector<u64>& ending
) {
    // At its arrival tick (`tick`), and from then on, the target a sword flies at is the one it strikes.
    const core::VolleyTarget& target = flight.targets.at(core::SwordTarget(flight.volley, sword, tick, flight.targets));
    // A target dead before this tick, or the point, takes nothing: the sword plunges. The renderer asks the same.
    if (!target.Struck(tick) || state.DomainOf(target.entity) != ACTIVE) {
        return;
    }
    core::Registry& entities = state[ACTIVE].Entities();
    const core::Entity entity = state[ACTIVE].Find(target.entity);
    const u64 use = flight.volley.use;
    events.push_back(
        {
            .kind = core::EventKind::Hit,
            .hit = flight.element,
            .use = use,
            .target = target.entity,
            .position = HeadOf(entities, manifest, entity),
            .value = flight.damage,
        }
    );
    Damage(state, target.entity, flight.damage, tick);
    if (!entities.all_of<Dying>(entity)) {
        Afflict(entities, entity, flight.element, flight.affliction, use, tick, ending);
    }
}

bool Combat::Holds(const core::State& state, u64 use) const {
    const auto pending = [use](const Flight& flight) { return flight.volley.use == use && flight.Pending(); };
    if (std::ranges::any_of(m_flights, pending)) {
        return true;
    }
    const auto afflicts = [use](const Afflictions& afflictions) {
        return (afflictions.burn.ticksLeft > 0 && afflictions.burn.use == use) ||
               (afflictions.poison.ticksLeft > 0 && afflictions.poison.use == use);
    };
    const core::Registry& entities = state[ACTIVE].Entities();
    return std::ranges::any_of(entities.view<const Afflictions>(), [&](core::Entity entity) {
        return afflicts(entities.get<Afflictions>(entity));
    });
}

void Combat::Strike(core::State& state, const core::Manifest& manifest, u64 tick, std::vector<core::Event>& events) {
    PN_PROFILE_ZONE();
    std::vector<u64> ending; // uses whose holds may have ended this tick
    FollowTargets(state, manifest);
    TickAfflictions(state, manifest, tick, events, ending);
    // After the afflictions due, as a sword's arrival is: a strike refreshing one due now deals that tick first.
    for (const LanceStrike& strike : m_strikes) {
        DealStrike(state, manifest, strike, tick, events, ending);
    }
    m_strikes.clear();
    // Every arrival resolves before this tick's deaths are marked, so a target dying on this tick still takes the hits
    // that arrive on it, as core::SwordTarget() promises.
    for (Flight& flight : m_flights) {
        // Its use may end on the Strike that resolves its last sword, whatever the tick (a loaded volley's arrivals may be
        // overdue), or on its cast for a volley of none.
        const bool pending = flight.Pending() || flight.volley.startTick == tick;
        while (flight.Pending() && flight.arrivals.at(flight.resolved) <= tick) {
            Arrive(state, manifest, flight, flight.order.at(flight.resolved), tick, events, ending);
            ++flight.resolved;
        }
        if (pending && !flight.Pending()) {
            ending.push_back(flight.volley.use);
        }
    }
    MarkDeaths(state, tick);
    EndDyingAfflictions(state[ACTIVE].Entities(), ending);
    EndReleased(state, ending, tick, events);
    std::erase_if(m_flights, [tick](const Flight& flight) {
        return !flight.Pending() && tick > flight.lastArrival + flight.volley.shape.fadeTicks;
    });
    // None starts after `tick`: a cast starts it at its tick, and Arena::Load() refuses one from a later tick.
    std::erase_if(m_flashes, [tick](const core::Flash& flash) { return tick - flash.startTick >= core::FLASH_TICKS; });
}

void Combat::EndReleased(const core::State& state, std::vector<u64>& ending, u64 tick, std::vector<core::Event>& events) const {
    std::ranges::sort(ending);
    const auto [last, end] = std::ranges::unique(ending);
    ending.erase(last, end);
    for (const u64 use : ending) {
        if (!Holds(state, use)) {
            events.push_back({ .kind = core::EventKind::UseEnded, .use = use });
            PN_LOG_DEBUG("tick {}: ability use {:#x} ends", tick, use);
        }
    }
}

void Combat::FollowTargets(core::State& state, const core::Manifest& manifest) {
    core::Domain& active = state[ACTIVE];
    for (Flight& flight : m_flights) {
        for (core::VolleyTarget& target : flight.targets) {
            if (target.diedTick != 0) {
                target.previous = target.current; // still where it died, between ticks too
            }
            else if (state.DomainOf(target.entity) == ACTIVE) {
                const Chest chest = ChestOf(active.Entities(), manifest, active.Find(target.entity));
                target.previous = chest.previous;
                target.current = chest.current;
            }
        }
    }
}

void Combat::MarkDeaths(const core::State& state, u64 tick) {
    const core::Domain& active = state[ACTIVE];
    for (Flight& flight : m_flights) {
        for (core::VolleyTarget& target : flight.targets) {
            if (target.diedTick == 0 &&
                (state.DomainOf(target.entity) != ACTIVE || active.Entities().all_of<Dying>(active.Find(target.entity)))) {
                target.diedTick = tick;
            }
        }
    }
}

void Combat::EndUses(const core::State& state, std::vector<core::Event>& events) const {
    std::vector<u64> open;
    for (const Flight& flight : m_flights) {
        if (flight.Pending()) {
            open.push_back(flight.volley.use);
        }
    }
    for (const auto [entity, afflictions] : state[ACTIVE].Entities().view<const Afflictions>().each()) {
        for (const AfflictionState* const affliction : { &afflictions.burn, &afflictions.poison }) {
            if (affliction->ticksLeft > 0) {
                open.push_back(affliction->use);
            }
        }
    }
    std::ranges::sort(open);
    const auto [last, end] = std::ranges::unique(open);
    open.erase(last, end);
    for (const u64 use : open) {
        events.push_back({ .kind = core::EventKind::UseEnded, .use = use });
    }
    if (!open.empty()) {
        PN_LOG_DEBUG("{} open ability uses end with the scene", open.size());
    }
}

void Combat::Publish(core::Snapshot& snapshot) const {
    PN_PROFILE_ZONE();
    for (const Flight& flight : m_flights) {
        snapshot.volleys.push_back(
            {
                .volley = flight.volley,
                .ability = flight.ability,
                .element = flight.element,
                .firstTarget = static_cast<u32>(snapshot.volleyTargets.size()),
                .targets = static_cast<u32>(flight.targets.size()),
            }
        );
        snapshot.volleyTargets.insert(snapshot.volleyTargets.end(), flight.targets.begin(), flight.targets.end());
    }
    snapshot.flashes.insert(snapshot.flashes.end(), m_flashes.begin(), m_flashes.end());
}

bool Combat::FlashesStartBy(u64 tick) const {
    return std::ranges::all_of(m_flashes, [tick](const core::Flash& flash) { return flash.startTick <= tick; });
}

bool Combat::AimsAtCreatures(const core::State& state) const {
    for (const Flight& flight : m_flights) {
        for (const core::VolleyTarget& target : flight.targets) {
            // Dead ones, the point among them (Load() refuses a living point), are never read again.
            if (target.diedTick == 0 && (state.DomainOf(target.entity) != ACTIVE ||
                                         !state[ACTIVE].Entities().all_of<Creature>(state[ACTIVE].Find(target.entity)))) {
                return false;
            }
        }
    }
    return true;
}

namespace {

// The respawns Combat::Save() wrote, failing `archive` on a count past what is left or ticks out of order.
std::vector<u64> ReadRespawns(core::Reader& archive) {
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
    return respawns;
}

// The flashes Combat::Save() wrote, failing `archive` on a count past what is left, a point that isn't finite or an
// unknown element.
std::vector<core::Flash> ReadFlashes(core::Reader& archive) {
    u64 count = 0;
    archive(count);
    // Each flash takes more than a u64, so a count past this is malformed.
    if (count > archive.Remaining() / sizeof(u64)) {
        archive.Fail();
    }
    std::vector<core::Flash> flashes;
    flashes.reserve(archive.Failed() ? 0 : static_cast<std::size_t>(count));
    for (u64 i = 0; i < count && !archive.Failed(); ++i) {
        core::Flash flash;
        archive(flash);
        if (!core::IsFinite(flash.point) || std::to_underlying(flash.element) >= core::HIT_KINDS) {
            archive.Fail();
        }
        flashes.push_back(flash);
    }
    return flashes;
}

} // namespace

void Combat::Load(core::Reader& archive) {
    std::vector<u64> respawns = ReadRespawns(archive);
    // Each volley and each target takes more than a u64, so a count past this is malformed.
    u64 count = 0;
    archive(count);
    if (count > archive.Remaining() / sizeof(u64)) {
        archive.Fail();
    }
    std::vector<Flight> flights;
    flights.reserve(archive.Failed() ? 0 : static_cast<std::size_t>(count));
    for (u64 i = 0; i < count && !archive.Failed(); ++i) {
        Flight flight;
        archive(flight.volley, flight.ability, flight.element, flight.damage, flight.affliction, flight.resolved);
        u64 targets = 0;
        archive(targets);
        if (archive.Failed() || targets == 0 || targets > archive.Remaining() / sizeof(u64) || !flight.volley.Valid() ||
            flight.ability == core::AbilityKind::None || std::to_underlying(flight.ability) >= core::ABILITY_KINDS ||
            std::to_underlying(flight.element) >= core::HIT_KINDS || flight.damage < 0 || !flight.affliction.Valid() ||
            flight.resolved > flight.volley.shape.count) {
            archive.Fail();
            break;
        }
        flight.targets.resize(static_cast<std::size_t>(targets));
        for (core::VolleyTarget& target : flight.targets) {
            archive(target);
            if (!core::IsFinite(target.previous) || !core::IsFinite(target.current) ||
                (target.entity == core::StableId{} && target.diedTick == 0)) {
                archive.Fail();
            }
        }
        if (!archive.Failed()) {
            flight.Prepare();
            flights.push_back(std::move(flight));
        }
    }
    std::vector<core::Flash> flashes = ReadFlashes(archive);
    if (!archive.Failed()) {
        m_respawns = std::move(respawns);
        m_flights = std::move(flights);
        m_flashes = std::move(flashes);
    }
}

} // namespace game
