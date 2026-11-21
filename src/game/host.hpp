#pragma once

/// @file
/// @ingroup game
/// The host every scene runs in (game/scene.hpp, listed in game/scenes.cpp): as the core::Simulation it owns the game's
/// world (game/world_state.hpp: the state's domains, the world NPCs, their journeys and travel journals (game/travel.hpp),
/// the calendar: the world clock, the month steps, the world pause, and what the player has explored of the world map
/// and how far its cultivator sees), made once and kept across scene changes, with
/// that world as it was made, which saves are written against, and a dev scene's scratch world while one runs; the player's
/// cultivator, the selection, combat, the game data, saving, loading and hashing, and the publishing every scene shares, and it
/// knows no scene by name.

#include "core/calendar.hpp"
#include "core/draw_list.hpp"
#include "core/ecs.hpp"
#include "core/frame.hpp"
#include "core/game_data.hpp"
#include "core/glow.hpp"
#include "core/journey.hpp"
#include "core/logic.hpp"
#include "core/manifest.hpp"
#include "core/rect.hpp"
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
    /// Makes the game's world from the seed and `data` (GenerateWorld()), keeping it as it was made, the base its saves are
    /// written against, and running a copy of it. Then builds `scene` with `data`'s creatures: the
    /// same entities, in the same places, every time. It draws its sprites where `manifest` puts them; `manifest` and `data`
    /// must pass CheckScenes(): the world scene needs `data.world`, which the default BuiltInData() lacks (build it with a map,
    /// and Load() refuses a save of it without).
    explicit Host(
        std::shared_ptr<const core::Manifest> manifest,
        core::SceneKey scene = ARENA_SCENE,
        core::GameData data = BuiltInData()
    );

    /// When `input` holds a CycleScene, asks for the next scene in the list's order (NextScene(): arena, stress, painted,
    /// cells, world, then the arena again; Requested()), ending every use still open (Combat::EndUses()) and, on the world
    /// map, keeping where the player's cultivator stands as the place it comes back to (none, so the start city, when that
    /// lies in the portal's step area), and does nothing else. Otherwise
    /// selects what its Select actions name (nothing for a dying one), ignores Cast actions outside a combat scene (in one a
    /// Cast is Combat::Cast(): Qi, cooldown, the ability's effect; a dash moves the player in place of held movement), pauses
    /// or resumes the world for a ToggleWorldPause in a scene showing the world map (ignored elsewhere), and sets the player's
    /// cultivator on a route for a WalkTo there (ignored elsewhere): the cheapest way on foot (core::FindRoute() by
    /// core::WALKING) from the tile under its foot to the tile under the action's point, as a journey of its own
    /// (PlayerRoute()) whose leg sets out from the foot itself on the world clock's tick. A WalkTo off the map or to a tile it
    /// can't reach changes nothing; one to the tile it stands on ends its route, leaving it where it stands. Then held movement
    /// ends its route and, unless the world is paused (when the cultivator stands, any route held), walks it where it points,
    /// and a route places it where core::Where() puts it at the tick the world clock reaches this tick, ending once the clock
    /// reaches the route's end, on its last tile's centre. When the player's foot, on its way this tick, crosses the step area
    /// of the scene's portal (Scene::Portal(): an ellipse PORTAL_RADIUS across and PORTAL_DEPTH_RATIO as deep, as its seal is
    /// drawn), it asks for the scene the portal leads to (NextWalkableScene(); Requested()), ending every use still open and,
    /// on the world map, keeping the point its foot stood on before this tick as the place it comes back to, off the portal
    /// (none, so the start city, when a month step's jump had left it standing in the step area), and does nothing more.
    /// Before that check, on the world map, the player's cultivator explores every tile within the sight radius
    /// (SightRadius()) of each tile its route reached this tick and of the tile under its foot (ExploredTiles()).
    /// Otherwise the world clock moves a tick in a scene showing the world map, unless the world is paused, a month is due
    /// (MonthDue()) or the calendar has ended (LAST_MONTH); it moves every entity on an Orbit to where it is at
    /// `counters.simulation`, advances combat (Combat::Tick(): arrivals, removing the dead, which clears their selection, and
    /// the foxes due back, which the scene respawns: Scene::Respawn()), and walks the active NPCs, whose decisions draw random
    /// numbers keyed on `counters.state`. Then the scene takes its own part of the tick (Scene::Tick(): in the arena, the
    /// crystal's aura); a DebugBurst action appends 4,000 hits round the foxes, which change nothing. Last, the volleys and
    /// afflictions strike (Combat::Strike()), appending their hits. Each use ends with a UseEnded event. The events themselves
    /// change nothing in the state.
    void Tick(const core::TickCounters& counters, const core::TickInput& input, std::vector<core::Event>& events) override;

    /// Lets a month pass for the running world's NPCs and counts it (Months()): a dev scene's scratch world while one
    /// runs, never the game's world set aside meanwhile. A step asked for before the month is due (M, a debug
    /// key) first moves the world clock on to the next month's first tick, and the player's cultivator on its route to where
    /// the new tick puts it, standing there for the tick (its route ending if the tick is past its end) and exploring round
    /// every tile of the route it reached on the way and the one under its foot, as a tick does; the world pause holds
    /// either way. Once the
    /// calendar has ended (LAST_MONTH) the step runs uncounted, the clock standing still. In a scene showing the world map
    /// the journeys over by the clock's tick end (Travel::Finish()) and the new month's visits set out (Travel::Month(),
    /// keyed on `counters.state`), unless the clock is past half an i64's range, where journeys' ticks would run out; then
    /// the NPCs at home take their month (MonthStep), the travellers sitting it out.
    void AdvanceCalendar(const core::TickCounters& counters, core::Jobs& jobs, const std::stop_token& stop) override;

    /// Whether the world clock has reached the end of the month after the last step run (TICKS_PER_MONTH a month): the
    /// clock stands still until AdvanceCalendar() runs.
    [[nodiscard]] bool MonthDue() const override;

    /// Writes into `archive` a header (the scene and the session's scene-ID counter), then the game's world as its
    /// difference from the world as it was made: the base's identity (the seed, GENERATOR_VERSION and the hash of the data
    /// it was made from, core::GameData::BaseHash()), the world-ID counter, the world clock, the month steps run, the
    /// world pause, the place on the map, the sight radius and the explored tiles whole, the world domain's difference
    /// (core::SaveDomainDelta()) and order (core::SaveDomainOrder()), the relationships whole and the travel's difference
    /// (Travel::SaveDelta(): its journeys rebuilt over the map to choose between a plan and a whole journey); then, while a dev
    /// scene runs, its scratch world as its difference from the world the scene's entry build made (its world-ID counter,
    /// clock, month steps, pause, place, its world domain's difference and order, its relationships, and its travel as its
    /// difference from none); then the scene (the player, the selection, the active domain in storage order, the player's route
    /// and the fight). It changes nothing, the order of every storage included, so a game that saves runs on as one that never
    /// did.
    void Save(core::Writer& archive) override;

    /// Reads and checks the state Save() wrote at simulation tick `simulationTick` and holds it, changing nothing else:
    /// Requested() names its scene until EnterScene() makes it current, its creatures taking the game data in use then.
    /// The game's world is made again from the save's seed and the game data in use (the base, kept as it was made), and
    /// the save's difference applied to a copy of it, the world to run; a dev scene's scratch world likewise from the
    /// scene's entry build (BuildScratch()). Each storage comes back in the order it was saved in.
    /// False, holding nothing new, when the bytes are malformed, of another version, from another GENERATOR_VERSION or
    /// world data (core::GameData::BaseHash(); each logged, saying which), with a game's world or a scratch world that
    /// differs from its base as no game could (core::LoadDomainDelta(), core::LoadDomainOrder() or Travel::LoadDelta()
    /// refusing it, or a world-ID counter behind the base's, each logged), don't end where the state does, hold a
    /// flash or a seal that starts after `simulationTick`, a world clock outside the month after the month steps run (its
    /// end included) or past the calendar's end (LAST_MONTH's first tick), relationships naming an NPC in neither domain or
    /// last met after their world's calendar, a place kept on the map that isn't a finite point or lies in its portal's step
    /// area, explored tiles that don't fit the map (core::FogBitsFit(): the wrong number of words, or a bit past its last
    /// tile), a sight radius past core::MAX_SIGHT, a world paused, keeping a place on the map, exploring or seeing any
    /// distance where it can't have shown the
    /// map (a dev scene's scratch world, or any in a game without the map), travel that Travel::LoadDelta() refuses (any at all
    /// in a scratch world or a game without the map), or a player's route that isn't a journey of the player alone with one
    /// tile route leg by core::WALKING of a step at least that LegSound() accepts, on the world map with its clock no further
    /// than journeys' ticks reach.
    [[nodiscard]] bool Load(core::Reader& archive, u64 simulationTick) override;

    /// A hash of everything Save() writes but the game's world while a dev scene runs, set aside where nothing changes it:
    /// the running world and the scene, in the state's current order.
    [[nodiscard]] u64 StateHash() const override;

    /// Takes `data` for the cultivator's walk speed from the next tick, and gives every creature its
    /// kind's sprite, orbit (radius, wobble, speed) and most health (its health kept, but never above it) at once; a fox
    /// may jump to its new orbit.
    void SetData(const core::GameData& data) override;

    /// A hash of the game's world alone, whole (its seed, world-ID counter, calendar, pause, place on the map, sight
    /// radius, explored tiles, world domain, relationships and travel, in the state's current order), whichever scene runs:
    /// unchanged by a dev scene, which runs on its own.
    [[nodiscard]] u64 WorldHash() const;

    /// The world the last dev scene's entry build made (BuildScratch()), kept for saves of its scratch world written
    /// against it and after the scene is left; null before any dev scene ran.
    [[nodiscard]] const WorldState* KeptScratchBase() const {
        return m_scratchBase.get();
    }

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

    /// Makes the scene Requested() names current: the worlds (the game's base among them) and the scene a load holds, its
    /// creatures taking the game data in use, or else the scene asked for, built with the selection, the fight and the
    /// player's route cleared, the last scene's entities gone and the world NPCs it brought in back in the world. A dev
    /// scene runs on a scratch world made afresh (its own NPCs and calendar), a game scene on the game's world, kept as it
    /// was (its NPCs, travel, calendar and pause), the world scene placing the cultivator where it last stood there. It
    /// forgets both.
    void EnterScene() override;

    /// How many month steps have run in the running world (a dev scene's own calendar while one runs).
    [[nodiscard]] u64 Months() const {
        return Live().months;
    }

    /// The world clock: the simulation ticks that ran in scenes showing the world map while the world wasn't paused and no
    /// month was due, plus those a month step asked for early skipped. The calendar (core::CalendarView) derives from it.
    /// The running world's (a dev scene's own while one runs).
    [[nodiscard]] u64 WorldTicks() const {
        return Live().worldTicks;
    }

    /// Whether the player has paused the running world (core::ActionKind::ToggleWorldPause): only in a scene showing the
    /// world map. The game's world keeps its pause while other scenes run; a dev scene's world is never paused.
    [[nodiscard]] bool WorldPaused() const {
        return Live().paused;
    }

    /// The running world's state (a dev scene's scratch world while one runs): the scene's entities in the active domain,
    /// NPCs in the world domain.
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

    /// The tiles of the world map the player has explored, one bit each, row by row (core/fog.hpp): the game's world's,
    /// whichever scene runs (as many words as the map's tiles need; none in a game without the map).
    [[nodiscard]] std::span<const u64> ExploredTiles() const {
        return m_world.explored;
    }

    /// How far the player's cultivator sees over the world map, in tiles: the game's world's, the game data's cultivator's
    /// (core::CreatureData::sight) in a new game, 0 in a game without the map.
    [[nodiscard]] u32 SightRadius() const {
        return m_world.sightRadius;
    }

    /// Sets how far the player's cultivator sees over the world map, at most core::MAX_SIGHT tiles: on the map, the next
    /// tick explores round its foot by it.
    void SetSightRadius(u32 radius);

    /// Explores every tile of the world map under `area`, its edges included, in the game's world: nothing for an area
    /// wholly off the map or with a NaN corner, or in a game without the map. For harnesses; the game explores only round
    /// the cultivator (Tick()).
    void Explore(const core::Rect& area);

    /// Changes whenever the explored tiles do and at each EnterScene(), never otherwise, so a publish can tell when to hand
    /// them on. Never saved or hashed.
    [[nodiscard]] u64 FogVersion() const {
        return m_fogVersion;
    }

    /// Sets whether the debug sight is on: every traveller in view is published as if in sight, and the fog's snapshot says
    /// so. It changes what is published only, never exploring or the state, so it is never recorded, saved or hashed.
    void SetDebugSight(bool enabled) override {
        m_debugSight = enabled;
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
    /// the centres of the route's tiles it hasn't reached by the world clock) and the fog (core::FogView: the explored
    /// tiles with FogVersion(), the tile under the player's foot when there is a player, the sight radius and the debug
    /// sight; off elsewhere). A scene with a player adds the player's and each living fox's name label over its head; a
    /// combat scene adds each living fox's health bar at its head and the player's casting state (core::Snapshot::caster).
    /// In a scene showing the world map each member of each journey under way at the world clock (core::Underway()) whose
    /// foot lies in the area and on a tile within one more than the sight radius of the player's (any tile with the debug
    /// sight on; none without either) is drawn as a villager with its shadow, both flagged to fade with sight
    /// (core::INSTANCE_SIGHT_FADE), its instance carrying its ID (so it can be picked and selected), placed by
    /// core::Where() at the clock when the last tick began and at the clock now (the same point while the clock stood
    /// still), packed in further chunks of the same size after the entities', and the player's cultivator draws over them
    /// and everything else in the world layer, so a crowd never hides it; with the game data's names (GameData::names) it
    /// carries a name label over its head (core::NameOf() of the state's seed and its ID, stacked by core::NameKey():
    /// LabelPriority::Selected when selected, LabelPriority::Named otherwise; hidden with sight, core::TextFog::Sight, its
    /// height the villager's foot height, so its anchor and height reach its foot), the labels following the player's in
    /// chunk order. A selected traveller out of sight stays selected, undrawn. Then the scene adds its own
    /// (Scene::Publish(): the tablet's caption, the arena's showcase seals, the world's place names), then a scene with a
    /// portal adds its seal (PORTAL_SEAL at SceneTraits::portal, twice PORTAL_RADIUS in size, from tick 0 and never ending,
    /// no showcase seal, so F8 never hides it) and its "Portal" caption over it, both whatever the view, as the seals are:
    /// one record each, whose look reaches past its point. After them come the volleys in flight with their targets and the
    /// seals raised in play (Combat::Publish()).
    void Publish(core::Snapshot& snapshot, core::Jobs& jobs, const std::optional<core::Rect>& view) override;

private:
    // Writes the scene's section of a save (or of the state's hash): the player, the selection, the active domain, the
    // player's route and the fight.
    template<typename Archive>
    void WriteScene(Archive& archive) const;

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
    // The way the running scene's hooks reach the running world, its player the host's.
    [[nodiscard]] WorldContext LiveContext();
    // The world dev scene `scene`'s entry build makes (BuildScratch()): what a save of its scratch world is written
    // against. The one kept from the last dev scene when it was that scene, else built afresh (its scene's entities left
    // in it, unread).
    [[nodiscard]] std::shared_ptr<const WorldState> ScratchBase(core::SceneKey scene) const;
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
    // Places the player on its route at world tick `worldTick`, exploring round each of its tiles reached after
    // `fromTick` (ExploreRoute()).
    void FollowRoute(u64 fromTick, u64 worldTick, u64 tick);
    // Whether the game's world's explored tiles are the size its map's fog takes, so exploring can write them.
    [[nodiscard]] bool FogFits() const;
    // Explores every tile within the sight radius of `tile` in the game's world, a new fog version when that uncovers any.
    void ExploreRound(u32 tile);
    // ExploreRound() of the tile under the player's foot, in a scene showing the world map with a player; nothing else.
    void ExploreRoundFoot();
    // The tile under the player's foot, the centre of its sight, in a scene showing the world map; none elsewhere or
    // without a player.
    [[nodiscard]] std::optional<u32> PlayerTile() const;
    // ExploreRound() of each tile `route`'s legs reach after world tick `fromTick` and by `toTick`.
    void ExploreRoute(const core::Journey& route, i64 fromTick, i64 toTick);
    [[nodiscard]] Body& PlayerBody();
    [[nodiscard]] const Body& PlayerBody() const;
    void CastAction(const core::Action& action, const core::TickCounters& counters, std::vector<core::Event>& events);
    void DebugBurst(const core::TickCounters& counters, std::vector<core::Event>& events) const;
    void Leave(core::SceneKey next, bool throughPortal, std::vector<core::Event>& events);
    [[nodiscard]] bool OnPortal();
    // Whether `point` lies in the step area of a scene showing the world map: no place the cultivator comes back to may.
    [[nodiscard]] bool OnMapPortal(core::Vec2 point);
    void PublishPortal(core::Snapshot& snapshot);
    void PublishOverhead(core::Snapshot& snapshot) const;
    void PublishCaster(core::Snapshot& snapshot) const;
    void PublishRoute(core::Snapshot& snapshot) const;
    // The fog of a scene showing the world map, its sight centred on `playerTile`.
    void PublishFog(core::Snapshot& snapshot, std::optional<u32> playerTile) const;
    // One job chunk of a publish: PUBLISH_CHUNK journeys' travellers in `area` and in sight of `playerTile` into draw-list
    // part `part` and their labels into m_travellerLabels[chunk], sharing nothing else with the other chunks, so they run
    // at once.
    void PublishJourneys(u32 chunk, u32 part, const core::Rect& area, std::optional<u32> playerTile);
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
    WorldState m_world; // the game's world
    WorldState m_base;  // the game's world as it was made, never ticked: what a save of it is written against
    u64 m_baseHash = 0; // the base hash of the data m_base was made from, which a save records
    std::optional<WorldState> m_scratch = std::nullopt; // the world a scene runs on in the game's place, if any
    // The last dev scene's scratch world as its entry build made it (m_scratch's while one runs): what a save of it is
    // written against. Never changed, and kept after the scene is left, so entering that scene again or loading a save
    // of it shares it rather than building it again.
    std::shared_ptr<const WorldState> m_scratchBase;
    core::SceneKey m_scratchBaseScene = ARENA_SCENE; // the dev scene m_scratchBase was built as, when there is one
    core::GameData m_data;
    core::SceneKey m_scene;
    core::SceneId m_sceneId; // m_scene's in the manifest
    // The world clock when the last tick began, where travellers are drawn moving from. Presentation only, so never saved
    // or hashed: a load or a jump of the clock sets it to the clock, and travellers stand still for a frame.
    u64 m_worldTicksBefore = 0;
    // Counts the changes of the game's world's explored tiles and the scenes entered, for the publish to tell when to hand
    // the fog on. Presentation only, so never saved or hashed.
    u64 m_fogVersion = 0;
    // Whether every traveller in view is published as if in sight. Presentation only, so never saved, hashed or recorded.
    bool m_debugSight = false;
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
            WorldState loadedBase,
            u64 loadedBaseHash,
            std::optional<WorldState> loadedScratch,
            std::shared_ptr<const WorldState> loadedScratchBase,
            Combat loadedCombat,
            core::SceneKey loadedScene,
            core::StableId loadedPlayer,
            core::StableId loadedSelected,
            std::optional<core::Journey> loadedRoute
        )
          : world(std::move(loadedWorld))
          , base(std::move(loadedBase))
          , baseHash(loadedBaseHash)
          , scratch(std::move(loadedScratch))
          , scratchBase(std::move(loadedScratchBase))
          , combat(std::move(loadedCombat))
          , scene(loadedScene)
          , player(loadedPlayer)
          , selected(loadedSelected)
          , route(std::move(loadedRoute)) {
        }

        WorldState world;
        WorldState base;
        u64 baseHash;
        std::optional<WorldState> scratch;
        std::shared_ptr<const WorldState> scratchBase;
        Combat combat;
        core::SceneKey scene;
        core::StableId player;
        core::StableId selected;
        std::optional<core::Journey> route;
    };
    std::optional<Loaded> m_loaded = std::nullopt;
};

} // namespace game
