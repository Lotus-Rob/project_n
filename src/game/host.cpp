#include "game/host.hpp"

#include "core/assert.hpp"
#include "core/cxx23.hpp" // IWYU pragma: keep
#include "core/instances.hpp"
#include "core/log.hpp"
#include "core/profile.hpp"
#include "core/random.hpp"
#include "core/seal.hpp"
#include "core/view_limits.hpp"
#include "game/random_purposes.hpp"

namespace game {

namespace {

using core::atlas::SpriteId;

// The demo glows' loops, in ticks (60 a second).
constexpr u64 CHARGE_LOOP = 192;    // 3.2 s
constexpr u64 CHARGE_RISE = 108;    // the value climbs from 0 to 1 over these, then holds
constexpr u64 CHARGE_RELEASE = 144; // and is released here
constexpr u64 PROGRESS_LOOP = 300;  // 5 s
constexpr u64 PROGRESS_FILL = 180;  // the value climbs over these, then the result shows
constexpr u64 PROGRESS_RESULTS = 4;

// The demo glow `sprite` wears in a scene whose traits list `glows`: none for the rest. The role isn't stored on the
// entity: that would change what saves hold and the state hash, and a glow is presentation only.
core::GlowRole DemoRoleOf(std::span<const DemoGlowSprite> glows, SpriteId sprite) {
    const auto found = std::ranges::find(glows, sprite, &DemoGlowSprite::sprite);
    return found == glows.end() ? core::GlowRole::None : found->role;
}

// A value rising linearly from 0 to 1 over `rise` ticks, then held.
f32 Rise(u64 age, u64 rise) {
    return std::min(static_cast<f32>(age) / static_cast<f32>(rise), 1.0F);
}

// The debug burst: DEBUG_BURST_HITS hits round the foxes, values from 1 to DEBUG_BURST_MAX.
constexpr u32 DEBUG_BURST_HITS = 4'000;
constexpr u32 DEBUG_BURST_MAX = 99;
constexpr u32 USE_DEBUG_BURST = 2; // the burst's ability use source (core::AbilityUse); the arena's aura is 1

// The world NPCs a scene asks for live over the map, a dozen to a cell.
constexpr u32 WORLD_CELLS = 4'096;
constexpr u64 SEED = 0x5eed;

// The portal as drawn: a ground seal twice its step area (PORTAL_RADIUS), so the step lands on its inner rings, and its
// caption PORTAL_RADIUS over its point, just past the far rim of the seal's squashed ellipse (PORTAL_DEPTH_RATIO of its
// size).
constexpr f32 PORTAL_SEAL_SIZE = 2.0F * PORTAL_RADIUS;
constexpr core::Vec2 PORTAL_LABEL_RISE{ .x = 0.0F, .y = -PORTAL_RADIUS };

// Halfway across a scene's view, where a still camera starts: what a publish culls round before the window's view arrives.
constexpr f32 MIDDLE = 0.5F;

// The host's part of a save: its format, then a version per domain, each changed with what its components save.
// 2: the player and the selection; 3: the respawns waiting, the volleys and the flashes; 4: the seals
constexpr u32 ARENA_FORMAT = 4;
constexpr u32 ACTIVE_VERSION = 4; // 2: creatures; 3: spin; 4: health, dying, arriving, caster, dashing, afflictions
constexpr u32 WORLD_VERSION = 1;
using ActiveComponents = core::ComponentList<
    Body,
    Look,
    Orbit,
    Spin,
    Creature,
    Npc,
    Needs,
    Met,
    Wander,
    Health,
    Dying,
    Arriving,
    Caster,
    Dashing,
    Afflictions>;
using WorldComponents = core::ComponentList<Npc, Needs, Met, Group>;

constexpr u32 PUBLISH_CHUNK = 4'096; // entities packed per job chunk
// Whether the active domain's combat components come as combat reads them: every creature has health and only
// creatures do, a creature is dying exactly when its health is out, an arriving one has the body and orbit it
// eases between, and only something with health is afflicted.
bool CombatConsistent(const core::Registry& entities) {
    const auto hasHealth = [&entities](core::Entity entity) { return entities.all_of<Health>(entity); };
    const auto livesOrDies = [&entities](core::Entity entity) {
        return entities.all_of<Creature>(entity) &&
               (entities.get<Health>(entity).current == 0) == entities.all_of<Dying>(entity);
    };
    const auto hasBody = [&entities](core::Entity entity) { return entities.all_of<Body>(entity); };
    const auto bodyAndOrbit = [&entities](core::Entity entity) { return entities.all_of<Body, Orbit>(entity); };
    return std::ranges::all_of(entities.view<const Creature>(), hasHealth) &&
           std::ranges::all_of(entities.view<const Health>(), livesOrDies) &&
           std::ranges::all_of(entities.view<const Dying>(), hasHealth) &&
           std::ranges::all_of(entities.view<const Arriving>(), bodyAndOrbit) &&
           std::ranges::all_of(entities.view<const Caster>(), hasHealth) &&
           std::ranges::all_of(entities.view<const Dashing>(), hasBody) &&
           std::ranges::all_of(entities.view<const Afflictions>(), hasHealth);
}

// Whether every entity has the components that code reads together with the ones it has: a save can
// hold any mix, and a missing partner would be read anyway (the month step reads an Npc's Needs,
// publishing a Look's Body, applying the game data a Creature's Look, deactivating an active Npc's Body).
bool Consistent(const core::State& state) {
    for (const core::DomainIndex domain : { ACTIVE, WORLD }) {
        const core::Registry& entities = state[domain].Entities();
        for (const core::Entity entity : entities.view<const Npc>()) {
            if (!entities.all_of<Needs>(entity) || (domain == ACTIVE && !entities.all_of<Body>(entity))) {
                return false;
            }
        }
        for (const core::Entity entity : entities.view<const Look>()) {
            if (!entities.all_of<Body>(entity)) {
                return false;
            }
        }
        for (const core::Entity entity : entities.view<const Creature>()) {
            if (!entities.all_of<Look>(entity)) {
                return false;
            }
        }
    }
    return CombatConsistent(state[ACTIVE].Entities()); // the world domain saves no combat components
}

core::SceneId SceneIdOf(const core::Manifest& manifest, core::SceneKey scene) {
    const std::optional<core::SceneId> found = manifest.SceneNamed(SceneName(scene));
    PN_ASSERT(found.has_value(), "the manifest lacks a scene: main checks it at start");
    return found.value_or(core::SceneId{});
}

// What assets/data/creatures.yaml and abilities.yaml ship. Parts rather than one GameData, which holds the world map's
// pointer and so is neither a constant expression nor free to destroy at exit.
constexpr core::CreatureData BUILT_IN_CULTIVATOR{ .sprite = SpriteId::Cultivator,
                                                  .health = 1'000,
                                                  .walkSpeed = 4.0F }; // 240 world units a second at full stick
// A lap in about 17 seconds.
constexpr core::CreatureData BUILT_IN_FOX{ .sprite = SpriteId::Fox,
                                           .health = 1'200,
                                           .orbitRadius = 300.0F,
                                           .orbitWobble = 40.0F,
                                           .orbitSpeed = 0.006F,
                                           .orbitsPlayer = true };
constexpr core::Abilities BUILT_IN_ABILITIES{
    .qiMax = 100,
    .qiStart = 50,
    .table = { {
        {},
        { .slot = core::SlotKind::Generator,
          .cooldownTicks = 15,
          .qiGain = 10,
          .damage = 140,
          .hit = core::HitKind::Burn,
          .radius = 120.0F,
          .affliction = { .ticks = 3, .damage = 40 },
          .speed = 25.0F },
        { .slot = core::SlotKind::Spender,
          .cooldownTicks = 30,
          .qiCost = 25,
          .damage = 360,
          .hit = core::HitKind::Poison,
          .radius = 90.0F,
          .affliction = { .ticks = 4, .damage = 60 } },
        { .slot = core::SlotKind::Movement, .cooldownTicks = 60, .distance = 260.0F, .durationTicks = 9 },
        { .slot = core::SlotKind::Special,
          .cooldownTicks = 360,
          .qiCost = 60,
          .damage = 20,
          .radius = 260.0F,
          .volley = { .count = 1'000,
                      .waves = 10,
                      .ringRadius = 160.0F,
                      .ringStep = 38.0F,
                      .scatter = 0.0F,
                      .angleJitter = 0.0F,
                      .staggerTicks = 3,
                      .waveGapTicks = 9,
                      .outTicks = 7,
                      .holdTicks = 18,
                      .departJitterTicks = 15,
                      .flightTicks = 102,
                      .sweep = 420.0F,
                      .sweepJitter = 0.6F,
                      .fadeTicks = 18 },
          .seal = core::SealCast{ .key = core::SealKeyOf("sword_formation"), .size = 420.0F } },
    } },
    .loadout = { {
        core::AbilityKind::FireBolt,
        core::AbilityKind::PoisonLance,
        core::AbilityKind::Dash,
        core::AbilityKind::FlyingSwords,
        core::AbilityKind::None,
        core::AbilityKind::None,
        core::AbilityKind::None,
        core::AbilityKind::None,
    } },
};

} // namespace

core::GameData BuiltInData() {
    return core::GameData{
        .cultivator = BUILT_IN_CULTIVATOR,
        .fox = BUILT_IN_FOX,
        .abilities = BUILT_IN_ABILITIES,
        .combat = core::CombatRules{}, // the shipped rules; a test checks that abilities.yaml says the same
        .world = nullptr,
    };
}

// What a scene's hooks reach the host through: only for the length of one call.
class Host::Context final : public SceneContext {
public:
    explicit Context(Host& host)
      : m_host{ &host } {
    }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;
    ~Context() override;

    [[nodiscard]] core::State& State() override {
        return m_host->m_state;
    }
    [[nodiscard]] const core::State& State() const override {
        return m_host->m_state;
    }
    [[nodiscard]] const core::Manifest& Manifest() const override {
        return *m_host->m_manifest;
    }
    [[nodiscard]] const core::GameData& Data() const override {
        return m_host->m_data;
    }
    core::Entity Spawn(core::Vec2 foot, const Look& look) override {
        return m_host->Spawn(foot, look);
    }
    core::Entity SpawnFox(const Orbit& orbit, core::Vec2 foot) override {
        return m_host->SpawnFox(orbit, foot);
    }
    core::Entity SpawnPlayer(core::Vec2 foot) override {
        return m_host->SpawnPlayer(foot);
    }
    [[nodiscard]] core::StableId Player() const override {
        return m_host->m_player;
    }
    void Damage(core::StableId target, i64 amount, u64 tick) override {
        m_host->Damage(target, amount, tick);
    }

private:
    Host* m_host;
};

Host::Context::~Context() = default; // out of line: the vtable's home

bool Host::IsCombatScene() const {
    return Running().Traits().combat;
}

core::SceneFacts Host::FactsOf(core::SceneKey scene) const {
    return {
        .key = scene,
        .sprites = SceneIdOf(*m_manifest, scene),
        .combat = SceneOf(scene).Traits().combat,
    };
}

std::optional<core::SceneFacts> Host::Requested() const {
    if (m_loaded) {
        return FactsOf(m_loaded->scene);
    }
    return m_requested.transform([this](core::SceneKey scene) { return FactsOf(scene); });
}

void Host::EnterScene() {
    if (m_loaded) {
        m_state = std::move(m_loaded->state);
        m_relationships = std::move(m_loaded->relationships);
        m_combat = std::move(m_loaded->combat);
        m_scene = m_loaded->scene;
        m_sceneId = SceneIdOf(*m_manifest, m_scene);
        m_months = m_loaded->months;
        m_player = m_loaded->player;
        m_selected = m_loaded->selected;
        m_loaded.reset();
        m_requested.reset(); // the load replaces the scene a CycleScene asked for
        ApplyData();
        return;
    }
    PN_ASSERT(m_requested.has_value(), "a scene entered that nothing asked for");
    if (!m_requested) {
        return; // with assertions off, the scene and its fight stay as they are
    }
    Build(*m_requested);
}

Host::Host(std::shared_ptr<const core::Manifest> manifest, core::SceneKey scene, core::GameData data)
  : m_manifest{ std::move(manifest) }
  , m_state{ DOMAINS, SEED }
  , m_data{ std::move(data) }
  , m_scene{ scene }
  , m_sceneId{ SceneIdOf(*m_manifest, m_scene) } {
    Build(m_scene);
}

// Everything but the calendar and the game data starts afresh: the entities, their IDs, the relationships, the
// selection.
void Host::Build(core::SceneKey scene) {
    m_state = core::State{ DOMAINS, SEED };
    m_relationships = Relationships{};
    m_combat = Combat{};
    m_combat.SetRules(m_data.combat);
    m_scene = scene;
    m_sceneId = SceneIdOf(*m_manifest, scene);
    m_requested.reset();
    m_player = {};
    m_selected = {};
    Context context{ *this };
    Running().Build(context);
    for (u32 npc = 0; npc < Running().Traits().worldNpcs; ++npc) {
        SpawnNpc(m_state, npc % WORLD_CELLS);
    }
}

core::Entity Host::Spawn(core::Vec2 foot, const Look& look) {
    const core::Entity entity = m_state.Create(ACTIVE).second;
    m_state[ACTIVE].Entities().emplace<Body>(entity, foot, foot);
    m_state[ACTIVE].Entities().emplace<Look>(entity, look);
    return entity;
}

core::Entity Host::SpawnFox(const Orbit& orbit, core::Vec2 foot) {
    const core::Entity entity = Spawn(foot, { .sprite = m_data.fox.sprite, .shadow = true });
    core::Registry& entities = m_state[ACTIVE].Entities();
    entities.emplace<Orbit>(entity, orbit);
    entities.emplace<Creature>(entity, CreatureKind::Fox);
    entities.emplace<Health>(entity, m_data.fox.health, m_data.fox.health);
    return entity;
}

core::Entity Host::SpawnPlayer(core::Vec2 foot) {
    const core::Entity entity = Spawn(foot, { .sprite = m_data.cultivator.sprite, .shadow = true });
    core::Registry& entities = m_state[ACTIVE].Entities();
    entities.emplace<Creature>(entity, CreatureKind::Cultivator);
    entities.emplace<Health>(entity, m_data.cultivator.health, m_data.cultivator.health);
    entities.emplace<Caster>(entity, m_data.abilities.qiStart);
    m_player = entities.get<core::StableId>(entity);
    return entity;
}

void Host::CastAction(const core::Action& action, const core::TickCounters& counters, std::vector<core::Event>& events) {
    if (IsCombatScene()) {
        m_combat.Cast(m_state, *m_manifest, m_data.abilities, m_player, action, counters, events);
        return;
    }
    PN_LOG_DEBUG("tick {}: a cast is ignored: the scene {} is no combat scene", counters.simulation, Running().Traits().name);
}

void Host::Tick(const core::TickCounters& counters, const core::TickInput& input, std::vector<core::Event>& events) {
    PN_PROFILE_ZONE();
    if (std::ranges::contains(input.actions, core::ActionKind::CycleScene, &core::Action::kind)) {
        const core::SceneKey next = NextScene(m_scene);
        PN_LOG_INFO("tick {}: the scene changes to {}", counters.simulation, SceneName(next));
        Leave(next, events);
        return;
    }
    for (const core::Action& action : input.actions) {
        if (action.kind == core::ActionKind::Select) {
            Select(action.target);
        }
        else if (action.kind == core::ActionKind::Cast) {
            CastAction(action, counters, events);
        }
    }
    if (!Combat::Dash(m_state, m_player, counters.simulation)) {
        Walk(input.held.move);
    }
    if (OnPortal()) {
        const core::SceneKey next = NextWalkableScene(m_scene);
        PN_LOG_INFO("tick {}: the cultivator steps through the portal to {}", counters.simulation, SceneName(next));
        Leave(next, events);
        return;
    }
    core::Domain& active = m_state[ACTIVE];
    if (m_player != core::StableId{}) {
        const core::Vec2 player = active.Entities().get<Body>(active.Find(m_player)).current;
        for (auto [entity, creature, orbit] : active.Entities().view<const Creature, Orbit>().each()) {
            if (DataOf(creature.kind).orbitsPlayer) {
                orbit.centre = player;
            }
        }
    }
    for (auto [entity, body, orbit] : active.Entities().view<Body, const Orbit>().each()) {
        body.previous = body.current;
        body.current = OrbitPoint(orbit, counters.simulation);
    }
    for (auto [entity, spin] : active.Entities().view<Spin>().each()) {
        spin.previous = spin.current;
        spin.current = std::remainder(spin.current + spin.speed, TURN); // stays precise however long it turns
    }
    const u32 respawns = m_combat.Tick(m_state, counters.simulation);
    // Before the respawns, which circle the player if there still is one.
    if (m_selected != core::StableId{} && !m_state.DomainOf(m_selected)) {
        PN_LOG_INFO("tick {}: the selected entity {} is gone", counters.simulation, std::to_underlying(m_selected));
        m_selected = {};
    }
    if (m_player != core::StableId{} && !m_state.DomainOf(m_player)) {
        PN_LOG_INFO("tick {}: the player's cultivator died and is gone", counters.simulation);
        m_player = {};
    }
    Context context{ *this };
    for (u32 index = 0; index < respawns; ++index) {
        Running().Respawn(context, counters, index);
    }
    DecideActive(active, m_state.Seed(), counters.state, counters.simulation);
    WalkActive(active);
    Running().Tick(context, counters, events);
    if (std::ranges::contains(input.actions, core::ActionKind::DebugBurst, &core::Action::kind)) {
        DebugBurst(counters, events);
    }
    // Last, once everything that moves or kills this tick has.
    m_combat.Strike(m_state, *m_manifest, counters.simulation, events);
}

void Host::Leave(core::SceneKey next, std::vector<core::Event>& events) {
    m_combat.EndUses(m_state, events); // nothing of this scene's goes on to hit
    m_requested = next;                // the logic has it built after this tick
}

bool Host::OnPortal() const {
    const std::optional<core::Vec2> portal = Running().Traits().portal;
    if (!portal || m_player == core::StableId{}) {
        return false;
    }
    // The foot's whole path this tick, not only where it ends: a dash moves farther in a tick than the step area is deep.
    // Measured in the step area's own units, where it is the unit circle.
    const Body& body = m_state[ACTIVE].Entities().get<Body>(m_state[ACTIVE].Find(m_player));
    const auto scaled = [&portal](core::Vec2 foot) {
        const core::Vec2 away = foot - *portal;
        return core::Vec2{ .x = away.x / PORTAL_RADIUS, .y = away.y / (PORTAL_RADIUS * PORTAL_DEPTH_RATIO) };
    };
    const core::Vec2 from = scaled(body.previous);
    const core::Vec2 path = scaled(body.current) - from;
    const f32 length = (path.x * path.x) + (path.y * path.y);
    const f32 along = length > 0.0F ? std::clamp(-((from.x * path.x) + (from.y * path.y)) / length, 0.0F, 1.0F) : 0.0F;
    const core::Vec2 nearest = from + (path * along);
    return (nearest.x * nearest.x) + (nearest.y * nearest.y) <= 1.0F;
}

void Host::PublishPortal(core::Snapshot& snapshot) const {
    PN_PROFILE_ZONE();
    const std::optional<core::Vec2> portal = Running().Traits().portal;
    if (!portal) {
        return;
    }
    snapshot.seals.push_back(
        { .key = PORTAL_SEAL, .point = *portal, .size = PORTAL_SEAL_SIZE, .startTick = 0, .endTick = core::NEVER_ENDS }
    );
    const core::Vec2 over = *portal + PORTAL_LABEL_RISE;
    snapshot.texts.push_back(
        {
            .kind = core::TextKind::Caption,
            .string = core::StringId::PortalLabel,
            .previous = over,
            .current = over,
            .align = core::TextAlign::BottomCentre,
        }
    );
}

void Host::PublishOverhead(core::Snapshot& snapshot) const {
    PN_PROFILE_ZONE();
    if (m_player == core::StableId{}) {
        return;
    }
    const core::Registry& entities = m_state[ACTIVE].Entities();
    const bool combat = IsCombatScene();
    for (auto [entity, creature, body, look, stableId, health] :
         entities.view<const Creature, const Body, const Look, const core::StableId, const Health>().each()) {
        const bool player = stableId == m_player;
        if ((!player && creature.kind != CreatureKind::Fox) || entities.all_of<Dying>(entity)) {
            continue;
        }
        const core::StringId name = player ? core::StringId::CultivatorName : core::StringId::FoxName;
        const f32 height = static_cast<f32>(m_manifest->Sprite(look.sprite).footY) * look.scale;
        const core::Vec2 head{ .x = 0.0F, .y = -height };
        if (combat && !player) {
            snapshot.healthBars.push_back(
                {
                    .entity = stableId,
                    .previous = body.previous + head,
                    .current = body.current + head,
                    .fraction = static_cast<f32>(static_cast<f64>(health.current) / static_cast<f64>(health.max)),
                }
            );
        }
        snapshot.texts.push_back(
            {
                .kind = core::TextKind::Label,
                .string = name,
                .previous = body.previous + head,
                .current = body.current + head,
                .offset = LABEL_OFFSET,
                .entity = stableId,
                .height = height,
                .nameKey = std::to_underlying(name),
                .priority = player ? core::LabelPriority::Player : core::LabelPriority::Rest,
            }
        );
    }
}

void Host::PublishCaster(core::Snapshot& snapshot) const {
    if (m_player == core::StableId{} || !IsCombatScene()) {
        return;
    }
    const core::Registry& entities = m_state[ACTIVE].Entities();
    const core::Entity player = m_state[ACTIVE].Find(m_player);
    const Caster* const caster = entities.try_get<Caster>(player);
    const Health* const health = entities.try_get<Health>(player);
    if (caster == nullptr || health == nullptr) {
        return;
    }
    snapshot.caster = core::CasterView{
        .qi = caster->qi,
        .qiMax = m_data.abilities.qiMax,
        .health = health->current,
        .healthMax = health->max,
        .readyTick = caster->readyTick,
        .loadout = m_data.abilities.loadout,
    };
}

void Host::DebugBurst(const core::TickCounters& counters, std::vector<core::Event>& events) const {
    PN_PROFILE_ZONE();
    const u64 use = core::AbilityUse(counters.simulation, USE_DEBUG_BURST);
    const core::Registry& entities = m_state[ACTIVE].Entities();
    std::vector<core::Entity> foxes;
    for (auto [entity, creature] : entities.view<const Creature>().each()) {
        if (creature.kind == CreatureKind::Fox) {
            foxes.push_back(entity);
        }
    }
    if (foxes.empty()) {
        PN_LOG_INFO("tick {}: the debug burst has no foxes to hit in this scene", counters.simulation);
    }
    else {
        events.reserve(events.size() + DEBUG_BURST_HITS + 1);
    }
    for (u32 index = 0; !foxes.empty() && index < DEBUG_BURST_HITS; ++index) {
        const core::Entity fox = foxes.at(index % foxes.size());
        const core::StableId target = entities.get<core::StableId>(fox);
        const u64 random = core::Random(m_state.Seed(), counters.state, std::to_underlying(target), PURPOSE_BURST, index);
        events.push_back(
            {
                .kind = core::EventKind::Hit,
                // The kinds take turns by round over the foxes, so each fox gets every kind whatever their number.
                .hit = static_cast<core::HitKind>((index / foxes.size()) % core::HIT_KINDS),
                .use = use,
                .target = target,
                .position = HeadOf(entities, *m_manifest, fox),
                .value = 1 + static_cast<i64>(core::ToBelow(random, DEBUG_BURST_MAX)),
            }
        );
    }
    events.push_back({ .kind = core::EventKind::UseEnded, .use = use });
}

void Host::Walk(core::Vec2 move) {
    if (m_player == core::StableId{}) {
        return;
    }
    core::Domain& active = m_state[ACTIVE];
    Body& body = active.Entities().get<Body>(active.Find(m_player));
    body.previous = body.current; // every tick, so a cultivator standing still draws still
    body.current = body.current + (move * m_data.cultivator.walkSpeed);
}

void Host::Select(core::StableId target) {
    // A dying creature can't be picked, as its instance names none.
    const bool dying =
        m_state.DomainOf(target) == ACTIVE && m_state[ACTIVE].Entities().all_of<Dying>(m_state[ACTIVE].Find(target));
    m_selected = m_state.DomainOf(target) && !dying ? target : core::StableId{};
    PN_LOG_INFO(
        "selected {}", m_selected == core::StableId{} ? "nothing" : std::format("entity {}", std::to_underlying(target))
    );
}

void Host::AdvanceCalendar(const core::TickCounters& counters, core::Jobs& jobs, const std::stop_token& stop) {
    m_monthStep.Run(m_state, m_relationships, jobs, counters.state, stop);
    ++m_months;
}

template<typename Archive>
void Host::Write(Archive& archive) const {
    // The scene stays one byte, as the scene enum was.
    archive(
        ARENA_FORMAT,
        static_cast<u8>(std::to_underlying(m_scene)),
        m_state.Seed(),
        m_state.Ids().next,
        m_months,
        m_player,
        m_selected
    );
    archive(ACTIVE_VERSION);
    core::SaveDomain(archive, m_state[ACTIVE], ActiveComponents{});
    archive(WORLD_VERSION);
    core::SaveDomain(archive, m_state[WORLD], WorldComponents{});
    m_relationships.Save(archive);
    m_combat.Save(archive);
}

void Host::Save(core::Writer& archive) {
    PN_PROFILE_ZONE();
    m_state.SortCanonical();
    Write(archive);
}

u64 Host::StateHash() const {
    PN_PROFILE_ZONE();
    core::Hasher hasher;
    Write(hasher);
    return hasher.Value();
}

bool Host::Load(core::Reader& archive, u64 simulationTick) {
    PN_PROFILE_ZONE();
    u32 format = 0;
    u8 scene = 0;
    u64 seed = 0;
    u64 next = 0;
    u64 months = 0;
    core::StableId player{};
    core::StableId selected{};
    archive(format, scene, seed, next, months, player, selected);
    if (format != ARENA_FORMAT || scene >= Scenes().size()) {
        archive.Fail();
    }
    core::State state{ DOMAINS, seed, core::StableIdCounter{ .next = next } };
    u32 version = 0;
    archive(version);
    if (version != ACTIVE_VERSION) {
        archive.Fail();
    }
    core::LoadDomain(archive, state, ACTIVE, ActiveComponents{});
    archive(version);
    if (version != WORLD_VERSION) {
        archive.Fail();
    }
    core::LoadDomain(archive, state, WORLD, WorldComponents{});
    Relationships relationships;
    relationships.Load(archive, state);
    Combat combat;
    combat.Load(archive);
    const bool playerFound = player == core::StableId{} || (state.DomainOf(player) == ACTIVE &&
                                                            state[ACTIVE].Entities().all_of<Body>(state[ACTIVE].Find(player)));
    if (archive.Failed() || archive.Remaining() != 0 || !Consistent(state) || !playerFound || !combat.AimsAtCreatures(state) ||
        !combat.FlashesStartBy(simulationTick) || !combat.SealsStartBy(simulationTick)) {
        return false;
    }
    m_loaded.emplace(
        std::move(state), std::move(relationships), std::move(combat), core::SceneKey{ scene }, months, player, selected
    );
    return true;
}

void Host::SetData(const core::GameData& data) {
    m_data = data;
    ApplyData();
}

void Host::ApplyData() {
    m_combat.SetRules(m_data.combat);
    core::Registry& entities = m_state[ACTIVE].Entities();
    for (auto [entity, creature, look] : entities.view<const Creature, Look>().each()) {
        const core::CreatureData& kind = DataOf(creature.kind);
        look.sprite = kind.sprite;
        if (Health* const health = entities.try_get<Health>(entity); health != nullptr && health->current > 0) {
            health->max = kind.health;
            health->current = std::min(health->current, kind.health);
        }
        if (Caster* const caster = entities.try_get<Caster>(entity)) {
            caster->qi = std::min(caster->qi, m_data.abilities.qiMax);
        }
        if (Orbit* const orbit = entities.try_get<Orbit>(entity)) {
            if (!kind.orbitsPlayer) {
                orbit->centre = Running().Traits().home; // back from wherever the player led it
            }
            orbit->radius = kind.orbitRadius;
            orbit->wobble = kind.orbitWobble;
            orbit->speed = kind.orbitSpeed;
        }
    }
}

core::GlowInput DemoGlow(core::GlowRole role, u64 tick) {
    if (role == core::GlowRole::DemoPulse) {
        return { .role = role };
    }
    if (role == core::GlowRole::DemoCharge) {
        const u64 age = tick % CHARGE_LOOP;
        const bool released = age >= CHARGE_RELEASE;
        return { .role = role,
                 .previousValue = age == 0 ? 0.0F : Rise(age - 1, CHARGE_RISE),
                 .currentValue = Rise(age, CHARGE_RISE),
                 .startTick = released ? tick - age + CHARGE_RELEASE : 0,
                 .released = released };
    }
    if (role == core::GlowRole::DemoProgress) {
        const u64 age = tick % PROGRESS_LOOP;
        const u8 result = age >= PROGRESS_FILL ? static_cast<u8>(1 + ((tick / PROGRESS_LOOP) % PROGRESS_RESULTS)) : u8{ 0 };
        return { .role = role,
                 .previousValue = age == 0 ? 0.0F : Rise(age - 1, PROGRESS_FILL),
                 .currentValue = Rise(age, PROGRESS_FILL),
                 .result = result };
    }
    return {};
}

void Host::Publish(core::Snapshot& snapshot, core::Jobs& jobs, const std::optional<core::Rect>& view) {
    PN_PROFILE_ZONE();
    const auto& bodies = m_state[ACTIVE].Entities().storage<Body>();
    const auto& looks = m_state[ACTIVE].Entities().storage<Look>();
    const auto& ids = m_state[ACTIVE].Entities().storage<core::StableId>();
    const auto& spins = m_state[ACTIVE].Entities().storage<Spin>();
    const auto& dyings = m_state[ACTIVE].Entities().storage<Dying>();
    const u64 tick = snapshot.simulationTick;
    const SceneTraits& traits = Running().Traits();
    const std::span<const DemoGlowSprite> demoGlows = traits.demoGlows;
    snapshot.combat = traits.combat;
    snapshot.camera = traits.camera;
    snapshot.worldMap = traits.worldMap;
    const u32 chunks = core::ChunkCount(looks.size(), PUBLISH_CHUNK);
    core::Vec2 start = (traits.view.min + traits.view.max) * MIDDLE;
    if (m_player != core::StableId{}) {
        const Body& body = bodies.get(m_state[ACTIVE].Find(m_player));
        snapshot.follow = core::Follow{ .previous = body.previous, .current = body.current };
        start = body.current;
    }
    // Before the main thread's view arrives the camera's zoom and window are unknown, so cull as widely as any shows.
    const core::Rect area =
        view.value_or(core::Rect{ .min = start - core::WIDEST_HALF_VIEW, .max = start + core::WIDEST_HALF_VIEW });
    const Context context{ *this };
    m_drawList.Begin(area, std::max(chunks, 1U));
    Running().PublishGround(context, area, m_drawList, 0);
    jobs.ParallelFor(chunks, [&](u32 chunk) {
        const std::size_t begin = std::size_t{ chunk } * PUBLISH_CHUNK;
        const std::size_t end = std::min(looks.size(), begin + PUBLISH_CHUNK);
        for (std::size_t i = begin; i < end; ++i) {
            const core::Entity entity = looks[i];
            const Body& body = bodies.get(entity);
            const Look& look = looks.get(entity);
            const core::StableId stableId = ids.get(entity);
            const Spin spin = spins.contains(entity) ? spins.get(entity) : Spin{};
            // A dying entity can't be picked, hovered or outlined: its instance names none.
            const bool dying = dyings.contains(entity);
            const f32 dissolve = dying ? DissolveAt(dyings.get(entity), tick, m_data.combat.dyingTicks) : 0.0F;
            if (look.shadow) {
                m_drawList.Add(
                    chunk,
                    core::Layer::Decals,
                    core::Pipeline::Normal,
                    core::MakeInstance(
                        *m_manifest,
                        { .sprite = SpriteId::Shadow, .previous = body.previous, .current = body.current, .dissolve = dissolve }
                    )
                );
            }
            m_drawList.Add(
                chunk,
                look.layer,
                look.pipeline,
                core::MakeInstance(
                    *m_manifest,
                    { .sprite = look.sprite,
                      .previous = body.previous,
                      .current = body.current,
                      .scale = look.scale,
                      .previousRotation = spin.previous,
                      .currentRotation = spin.current,
                      .dissolve = dissolve,
                      .glow = DemoGlow(DemoRoleOf(demoGlows, look.sprite), tick) }
                ),
                body.current.y,
                dying ? core::StableId{} : stableId
            );
        }
    });
    m_drawList.Publish(snapshot, jobs);
    PublishOverhead(snapshot);
    PublishCaster(snapshot);
    // Before the seals raised in play, which draw over the scene's own.
    Running().Publish(context, area, snapshot);
    PublishPortal(snapshot);
    m_combat.Publish(snapshot);
    if (m_selected != core::StableId{}) {
        if (const auto drawn = std::ranges::find(snapshot.ids, m_selected); drawn != snapshot.ids.end()) {
            snapshot.selected = static_cast<u32>(drawn - snapshot.ids.begin());
        }
    }
}

} // namespace game
