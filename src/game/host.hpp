#pragma once

/// @file
/// @ingroup game
/// The host every scene runs in (game/scene.hpp, listed in game/scenes.cpp): as the core::Simulation it owns the
/// state's domains, the world NPCs, their journeys and travel journals (game/travel.hpp) and the calendar (the world clock, the
/// month steps, the world pause), the player's cultivator, the selection, combat, the game data, saving, loading and hashing,
/// and the publishing every scene shares, and it knows no scene by name.

#include "core/calendar.hpp"
#include "core/draw_list.hpp"
#include "core/ecs.hpp"
#include "core/frame.hpp"
#include "core/game_data.hpp"
#include "core/glow.hpp"
#include "core/journey.hpp"
#include "core/logic.hpp"
#include "core/manifest.hpp"
#include "core/state.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"
#include "core/vec2.hpp"
#include "game/combat.hpp"
#include "game/components.hpp"
#include "game/npcs.hpp"
#include "game/scene.hpp"
#include "game/scenes.hpp"
#include "game/travel.hpp"
#include "game/world_state.hpp"

#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace game {

/// The test bed's game data as built in: what `assets/data/creatures.yaml` ships, for runs that read
/// no data files (tests, tools).
[[nodiscard]] core::GameData BuiltInData();

/// How many simulation ticks a day of world time lasts (3 s at 60 Hz): core's, which journeys count their stays by.
inline constexpr u64 TICKS_PER_DAY = core::TICKS_PER_DAY;
/// How many days a month lasts: core's, over whose days a month's visits set out.
inline constexpr u64 DAYS_PER_MONTH = core::DAYS_PER_MONTH;
/// How many simulation ticks a month of world time lasts: its last ends in the month step (Host::MonthDue()).
inline constexpr u64 TICKS_PER_MONTH = core::TICKS_PER_MONTH;
/// The last month the world clock reaches, the last whose first tick a u64 holds: at that tick the calendar ends, the clock
/// standing still and the month steps no longer counted, so neither ever wraps.
inline constexpr u64 LAST_MONTH = ~u64{ 0 } / TICKS_PER_MONTH;

/// The input of the demo glow `role` at simulation `tick` (the shrine's pulse, the stele's charge on a 3.2 s loop that rises,
/// holds and releases, the crystal's progress on a 5 s loop that fills and then shows a result): pure and presentation only.
/// Its previous value is the one at `tick - 1` in the same loop, 0 at a loop's first tick. Any other role gives no glow.
[[nodiscard]] core::GlowInput DemoGlow(core::GlowRole role, u64 tick);

/// The host as a simulation: it runs the scene it is (listed in game/scenes.cpp), moves its creatures each tick and publishes
/// every sprite.
class Host final : public core::Simulation {
public:
    /// Builds `scene` with `data`'s creatures: the same entities, in the same places, every time. It
    /// draws its sprites where `manifest` puts them; `manifest` and `data` must pass CheckScenes(): the world scene needs
    /// `data.world`, which the default BuiltInData() lacks (build it with a map, and Load() refuses a save of it without).
    explicit Host(
        std::shared_ptr<const core::Manifest> manifest,
        core::SceneKey scene = ARENA_SCENE,
        core::GameData data = BuiltInData()
    );

    /// When `input` holds a CycleScene, asks for the next scene in the list's order (NextScene(): arena, stress, painted,
    /// cells, world, then the arena again; Requested()), ending every use still open (Combat::EndUses()), and does nothing
    /// else. Otherwise selects what its Select actions name (nothing for a dying one), ignores Cast actions outside a combat
    /// scene (in one a Cast is Combat::Cast(): Qi, cooldown, the ability's effect; a dash moves the player in place of held
    /// movement), pauses or resumes the world for a ToggleWorldPause in a scene showing the world map (ignored elsewhere),
    /// and sets the player's cultivator on a route for a WalkTo there (ignored elsewhere): the cheapest way on foot
    /// (core::FindRoute() by core::WALKING) from the tile under its foot to the tile under the action's point, as a journey
    /// of its own (PlayerRoute()) whose leg sets out from the foot itself on the world clock's tick. A WalkTo off the map
    /// or to a tile it can't reach changes nothing; one to the tile it stands on ends its route, leaving it where it
    /// stands. Then held movement ends its route and, unless the world is paused (when the cultivator stands, any route
    /// held), walks it where it points, and a route places it where core::Where() puts it at the tick the world clock reaches
    /// this tick, ending once the clock reaches the route's end, on its last tile's centre. When the player's foot, on its way
    /// this tick, crosses the step area of the scene's portal (Scene::Portal(): an ellipse PORTAL_RADIUS across and
    /// PORTAL_DEPTH_RATIO as deep, as its seal is drawn), it asks for the scene the portal leads to (NextWalkableScene();
    /// Requested()), ending every use still open, and does nothing more. Otherwise the world clock moves a tick in a scene
    /// showing the world map, unless the world is paused, a month is due (MonthDue()) or the calendar has ended (LAST_MONTH);
    /// it moves every entity on an Orbit to where it is at `counters.simulation`, advances combat (Combat::Tick(): arrivals,
    /// removing the dead, which clears their selection, and the foxes due back, which the scene respawns: Scene::Respawn()),
    /// and walks the active NPCs, whose decisions draw random numbers keyed on `counters.state`. Then the scene takes its own
    /// part of the tick (Scene::Tick(): in the arena, the crystal's aura); a DebugBurst action appends 4,000 hits round the
    /// foxes, which change nothing. Last, the volleys and afflictions strike (Combat::Strike()), appending their hits. Each use
    /// ends with a UseEnded event. The events themselves change nothing in the state.
    void Tick(const core::TickCounters& counters, const core::TickInput& input, std::vector<core::Event>& events) override;

    /// Lets a month pass for the world NPCs and counts it (Months()). A step asked for before the month is due (M, a debug
    /// key) first moves the world clock on to the next month's first tick, and the player's cultivator on its route to where
    /// the new tick puts it, standing there for the tick (its route ending if the tick is past its end); the world pause
    /// holds either way. Once the
    /// calendar has ended (LAST_MONTH) the step runs uncounted, the clock standing still. In a scene showing the world map
    /// the journeys over by the clock's tick end (Travel::Finish()) and the new month's visits set out (Travel::Month(),
    /// keyed on `counters.state`), unless the clock is past half an i64's range, where journeys' ticks would run out; then
    /// the NPCs at home take their month (MonthStep), the travellers sitting it out.
    void AdvanceCalendar(const core::TickCounters& counters, core::Jobs& jobs, const std::stop_token& stop) override;

    /// Whether the world clock has reached the end of the month after the last step run (TICKS_PER_MONTH a month): the
    /// clock stands still until AdvanceCalendar() runs.
    [[nodiscard]] bool MonthDue() const override;

    /// Sorts the state into canonical order and writes it, with the scene, the world clock, the month steps run, the world
    /// pause, the relationships, the travel (the journeys and the journals) and the player's route, into `archive`.
    void Save(core::Writer& archive) override;

    /// Reads and checks the state Save() wrote at simulation tick `simulationTick` and holds it, changing nothing else:
    /// Requested() names its scene until EnterScene() makes it current, its creatures taking the game data in use then.
    /// False, holding nothing new, when the bytes are malformed, of another version, don't end where the state does, hold a
    /// flash or a seal that starts after `simulationTick`, a world clock outside the month after the month steps run (its
    /// end included) or past the calendar's end (LAST_MONTH's first tick), the world paused in a scene that shows no world
    /// map, travel that Travel::Load() refuses (any at all in a scene that shows no world map), or a player's route that
    /// isn't a journey of the player alone with one tile route leg by core::WALKING of a step at least that LegSound()
    /// accepts, on the world map with its clock no further than journeys' ticks reach.
    [[nodiscard]] bool Load(core::Reader& archive, u64 simulationTick) override;

    /// A hash of everything Save() writes, in the state's current order.
    [[nodiscard]] u64 StateHash() const override;

    /// Takes `data` for the cultivator's walk speed from the next tick, and gives every creature its
    /// kind's sprite, orbit (radius, wobble, speed) and most health (its health kept, but never above it) at once; a fox
    /// may jump to its new orbit.
    void SetData(const core::GameData& data) override;

    /// The hash of the game data in use.
    [[nodiscard]] u64 DataHash() const override {
        return m_data.Hash();
    }

    /// The manifest's scene for the scene it is: the one it was built as, or the one a load brought.
    [[nodiscard]] core::SceneId Scene() const override {
        return m_sceneId;
    }

    /// The scene it is: its key in the list (game/scenes.cpp), its manifest scene, and whether it is a combat scene.
    [[nodiscard]] core::SceneFacts Current() const override {
        return FactsOf(m_scene);
    }

    /// The scene of the state a load holds, or else the scene the last CycleScene asked for, until EnterScene(); none
    /// otherwise.
    [[nodiscard]] std::optional<core::SceneFacts> Requested() const override;

    /// Makes the scene Requested() names current: the state a load holds, its creatures taking the game data in use, or
    /// else the scene asked for, rebuilt with the selection, the world pause, the travel and the player's route cleared and
    /// the calendar kept. It forgets both.
    void EnterScene() override;

    /// How many month steps have run.
    [[nodiscard]] u64 Months() const {
        return Live().months;
    }

    /// The world clock: the simulation ticks that ran in scenes showing the world map while the world wasn't paused and no
    /// month was due, plus those a month step asked for early skipped. The calendar (core::CalendarView) derives from it.
    [[nodiscard]] u64 WorldTicks() const {
        return Live().worldTicks;
    }

    /// Whether the player has paused the world (core::ActionKind::ToggleWorldPause): only in a scene showing the world map,
    /// and only until the next scene is built.
    [[nodiscard]] bool WorldPaused() const {
        return Live().paused;
    }

    /// The game state: the scene's entities in the active domain, NPCs in the world domain.
    [[nodiscard]] const core::State& State() const {
        return Live().state;
    }

    /// Where the running scene's portal stands in this game (Scene::Portal()), or none when it has none.
    [[nodiscard]] std::optional<core::Vec2> Portal();

    /// The running scene's view in this game (Scene::View()): the map's size on the world map.
    [[nodiscard]] core::Rect View();

    /// Whether the scene is a combat scene, where fights happen and casts act (SceneTraits::combat): the arena only.
    [[nodiscard]] bool IsCombatScene() const;

    /// The cultivator the player walks, or zero in a scene without one (the painted scene, the cells).
    [[nodiscard]] core::StableId Player() const {
        return m_player;
    }

    /// The entity the player selected last, or zero for none.
    [[nodiscard]] core::StableId Selected() const {
        return m_selected;
    }

    /// Deals `amount` to `target` at simulation tick `tick` (Combat::Damage()): a negative amount heals.
    void Damage(core::StableId target, i64 amount, u64 tick) {
        Combat::Damage(Live().state, target, amount, tick);
    }

    /// What the NPCs think of each other.
    [[nodiscard]] const Relationships& NpcRelationships() const {
        return Live().relationships;
    }

    /// The world NPCs' journeys under way and their travel journals.
    [[nodiscard]] const Travel& NpcTravel() const {
        return Live().travel;
    }

    /// The route the player's cultivator walks on the world map (core::ActionKind::WalkTo): a journey of the cultivator alone
    /// with one tile route leg by core::WALKING, from the foot it set out from to the tile walked to; none while it walks
    /// freely.
    [[nodiscard]] const std::optional<core::Journey>& PlayerRoute() const {
        return m_route;
    }

    /// Publishes the scene's ground (Scene::PublishGround(): the painted ground, or the world's tiles), then a shadow under
    /// each entity that casts one, then the world's entities in foot-y order, then the effects, packed in fixed chunks on
    /// `jobs`: the bytes are the same whatever its worker count. Each entity's instance carries its ID, but a dying one's
    /// carries none and dissolves (core::SpriteDraw::dissolve, DissolveAt() at `snapshot.simulationTick`), its shadow with
    /// it; the sprites a scene's traits list (SceneTraits::demoGlows) wear their demo glows (DemoGlow()). What is drawn is
    /// culled to `view` (the main thread's window and its margin) or, with none yet, to the widest view a window shows
    /// (core::WIDEST_HALF_VIEW) round where the camera starts: the player's cultivator, which is the one to follow, or else
    /// the centre of the scene's view (SceneTraits::view); the painted ground is drawn whole either way, and the scene's
    /// hooks get the same area. The snapshot names the scene's camera (SceneTraits::camera) and, in a scene showing the
    /// world map, the calendar (core::CalendarView: the months and the day of the month the world clock has reached, and
    /// the world pause) and, while the player's cultivator walks a route, the route (core::Snapshot::route: its foot, then
    /// the centres of the route's tiles it hasn't reached by the world clock). A scene with a player adds the
    /// player's and each living fox's name label over its head; a combat scene adds each living fox's health bar at its
    /// head and the player's casting state (core::Snapshot::caster). In a scene showing the world map each member of each
    /// journey under way at the world clock (core::Underway()) whose foot lies in the area is drawn as a villager with
    /// its shadow, its instance carrying its ID (so it can be picked and selected), placed by core::Where() at the clock
    /// when the last tick began and at the clock now (the same point while the clock stood still), packed in further
    /// chunks of the same size after the entities', and the player's cultivator draws over them and everything else in
    /// the world layer, so a crowd never hides it; with the game data's names (GameData::names) it carries a name label
    /// over its head (core::NameOf() of the state's seed and its ID, stacked by core::NameKey(): LabelPriority::Selected
    /// when selected, LabelPriority::Named otherwise), the labels following the player's in chunk order. Then the scene
    /// adds its own (Scene::Publish(): the tablet's caption, the arena's showcase seals, the world's place names), then
    /// a scene with a portal adds its seal (PORTAL_SEAL at SceneTraits::portal, twice PORTAL_RADIUS in size, from tick 0
    /// and never ending, no showcase seal, so F8 never hides it) and its "Portal" caption over it, both whatever the view,
    /// as the seals are: one record each, whose look reaches past its point. After them come the volleys in flight with
    /// their targets and the seals raised in play (Combat::Publish()).
    void Publish(core::Snapshot& snapshot, core::Jobs& jobs, const std::optional<core::Rect>& view) override;

private:
    template<typename Archive>
    void Write(Archive& archive) const;

    class Context; // the SceneContext a scene's hooks reach the host through

    [[nodiscard]] const game::Scene& Running() const {
        return SceneOf(m_scene);
    }
    // The world the running scene plays on: the scratch one while there is one, the game's otherwise.
    [[nodiscard]] WorldState& Live() {
        return m_scratch ? *m_scratch : m_world;
    }
    [[nodiscard]] const WorldState& Live() const {
        return m_scratch ? *m_scratch : m_world;
    }
    void Build(core::SceneKey scene);
    [[nodiscard]] core::SceneFacts FactsOf(core::SceneKey scene) const;
    core::Entity Spawn(core::Vec2 foot, const Look& look);
    core::Entity SpawnFox(const Orbit& orbit, core::Vec2 foot);
    core::Entity SpawnPlayer(core::Vec2 foot);
    void Walk(core::Vec2 move);
    void Select(core::StableId target);
    void TakeActions(
        std::span<const core::Action> actions,
        const core::TickCounters& counters,
        std::vector<core::Event>& events
    );
    void ToggleWorldPause(u64 tick);
    void WalkTo(core::Vec2 point, u64 tick);
    void Move(core::Vec2 move, u64 worldTick, u64 tick);
    void FollowRoute(u64 worldTick, u64 tick);
    [[nodiscard]] Body& PlayerBody();
    [[nodiscard]] const Body& PlayerBody() const;
    void SeedTravel();
    void CastAction(const core::Action& action, const core::TickCounters& counters, std::vector<core::Event>& events);
    void DebugBurst(const core::TickCounters& counters, std::vector<core::Event>& events) const;
    void Leave(core::SceneKey next, std::vector<core::Event>& events);
    [[nodiscard]] bool OnPortal();
    void PublishPortal(core::Snapshot& snapshot);
    void PublishOverhead(core::Snapshot& snapshot) const;
    void PublishCaster(core::Snapshot& snapshot) const;
    void PublishRoute(core::Snapshot& snapshot) const;
    // One job chunk of a publish: PUBLISH_CHUNK journeys' travellers into draw-list part `part` and their labels into
    // m_travellerLabels[chunk], sharing nothing else with the other chunks, so they run at once.
    void PublishJourneys(u32 chunk, u32 part, const core::Rect& area);
    // How many PUBLISH_CHUNK chunks of journeys a publish draws: none off the world map.
    [[nodiscard]] u32 JourneyChunks() const;
    void ApplyData();
    [[nodiscard]] const core::CreatureData& DataOf(CreatureKind kind) const {
        return kind == CreatureKind::Fox ? m_data.fox : m_data.cultivator;
    }

    core::DrawList m_drawList;
    MonthStep m_monthStep;
    Combat m_combat;
    std::vector<std::vector<core::TextItem>> m_travellerLabels; // per publish chunk, kept for their capacity
    std::optional<core::Paths> m_paths = std::nullopt; // the world map's distance maps, kept for the run; none without one
    std::shared_ptr<const core::Manifest> m_manifest;
    WorldState m_world;                                 // the game's world
    std::optional<WorldState> m_scratch = std::nullopt; // the world a scene runs on in the game's place, if any
    core::GameData m_data;
    core::SceneKey m_scene;
    core::SceneId m_sceneId; // m_scene's in the manifest
    // The world clock when the last tick began, where travellers are drawn moving from. Presentation only, so never saved
    // or hashed: a load or a jump of the clock sets it to the clock, and travellers stand still for a frame.
    u64 m_worldTicksBefore = 0;
    std::optional<core::Journey> m_route = std::nullopt; // the player's route; none while it walks freely
    core::StableId m_player{};                           // the cultivator the player walks; zero for none
    core::StableId m_selected{};                         // zero for none

    // The scene a CycleScene asked for, until it is entered.
    std::optional<core::SceneKey> m_requested = std::nullopt;

    // A sound load's state, held until EnterScene() makes it current. A constructor, not an aggregate: WorldState has no
    // default, and MSVC warns (C4623) where an aggregate's default constructor is implicitly deleted.
    struct Loaded {
        Loaded(
            WorldState loadedWorld,
            Combat loadedCombat,
            core::SceneKey loadedScene,
            core::StableId loadedPlayer,
            core::StableId loadedSelected,
            std::optional<core::Journey> loadedRoute
        )
          : world(std::move(loadedWorld))
          , combat(std::move(loadedCombat))
          , scene(loadedScene)
          , player(loadedPlayer)
          , selected(loadedSelected)
          , route(std::move(loadedRoute)) {
        }

        WorldState world;
        Combat combat;
        core::SceneKey scene;
        core::StableId player;
        core::StableId selected;
        std::optional<core::Journey> route;
    };
    std::optional<Loaded> m_loaded = std::nullopt;
};

} // namespace game
