#pragma once

/// @file
/// @ingroup game
/// The host every scene runs in (game/scene.hpp, listed in game/scenes.cpp): as the core::Simulation it owns the
/// state's domains, the world NPCs and the calendar, the player's cultivator, the selection, combat, the game data,
/// saving, loading and hashing, and the publishing every scene shares, and it knows no scene by name.

#include "core/draw_list.hpp"
#include "core/ecs.hpp"
#include "core/frame.hpp"
#include "core/game_data.hpp"
#include "core/glow.hpp"
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

#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

namespace game {

/// The test bed's game data as built in: what `assets/data/creatures.yaml` ships, for runs that read
/// no data files (tests, tools).
[[nodiscard]] core::GameData BuiltInData();

/// The input of the demo glow `role` at simulation `tick` (the shrine's pulse, the stele's charge on a 3.2 s loop that rises,
/// holds and releases, the crystal's progress on a 5 s loop that fills and then shows a result): pure and presentation only.
/// Its previous value is the one at `tick - 1` in the same loop, 0 at a loop's first tick. Any other role gives no glow.
[[nodiscard]] core::GlowInput DemoGlow(core::GlowRole role, u64 tick);

/// The host as a simulation: it runs the scene it is (listed in game/scenes.cpp), moves its creatures each tick and publishes
/// every sprite.
class Host final : public core::Simulation {
public:
    /// Builds `scene` with `data`'s creatures: the same entities, in the same places, every time. It
    /// draws its sprites where `manifest` puts them; `manifest` must pass CheckScenes().
    explicit Host(
        std::shared_ptr<const core::Manifest> manifest,
        core::SceneKey scene = ARENA_SCENE,
        const core::GameData& data = BuiltInData()
    );

    /// When `input` holds a CycleScene, asks for the next scene in the list's order (NextScene(): arena, stress, painted,
    /// cells, world, then the arena again; Requested()), ending every use still open (Combat::EndUses()), and does nothing
    /// else. Otherwise walks the player's cultivator where `input`'s held movement points, selects what its Select actions name
    /// (nothing for a dying one), ignores Cast actions outside a combat scene (in one a Cast is Combat::Cast(): Qi,
    /// cooldown, the ability's effect; a dash moves the player in place of held movement). When the player's foot, on its way
    /// this tick, crosses the step area of the scene's portal (SceneTraits::portal: an ellipse PORTAL_RADIUS across and
    /// PORTAL_DEPTH_RATIO as deep, as its seal is drawn), it asks for the scene the portal leads to
    /// (NextWalkableScene(); Requested()), ending every use still open, and does nothing more. Otherwise it moves every entity
    /// on an Orbit to where it is at `counters.simulation`, advances combat (Combat::Tick(): arrivals, removing the dead, which
    /// clears their selection, and the foxes due back, which the scene respawns: Scene::Respawn()), and walks the active NPCs,
    /// whose decisions draw random numbers keyed on `counters.state`. Then the scene takes its own part of the tick
    /// (Scene::Tick(): in the arena, the crystal's aura); a DebugBurst action appends 4,000 hits round the foxes, which
    /// change nothing. Last, the volleys and afflictions strike (Combat::Strike()), appending their hits. Each use ends with a
    /// UseEnded event. The events themselves change nothing in the state.
    void Tick(const core::TickCounters& counters, const core::TickInput& input, std::vector<core::Event>& events) override;

    /// Lets a month pass for the world NPCs (MonthStep).
    void AdvanceCalendar(const core::TickCounters& counters, core::Jobs& jobs, const std::stop_token& stop) override;

    /// Sorts the state into canonical order and writes it, with the scene, the months passed and
    /// the relationships, into `archive`.
    void Save(core::Writer& archive) override;

    /// Reads and checks the state Save() wrote at simulation tick `simulationTick` and holds it, changing nothing else:
    /// Requested() names its scene until EnterScene() makes it current, its creatures taking the game data in use then.
    /// False, holding nothing new, when the bytes are malformed, of another version, don't end where the state does, or
    /// hold a flash or a seal that starts after `simulationTick`.
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
    /// else the scene asked for, rebuilt with the selection cleared. It forgets both.
    void EnterScene() override;

    /// How many months have passed.
    [[nodiscard]] u64 Months() const {
        return m_months;
    }

    /// The game state: the scene's entities in the active domain, NPCs in the world domain.
    [[nodiscard]] const core::State& State() const {
        return m_state;
    }

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
        Combat::Damage(m_state, target, amount, tick);
    }

    /// What the NPCs think of each other.
    [[nodiscard]] const Relationships& NpcRelationships() const {
        return m_relationships;
    }

    /// Publishes the scene's ground (Scene::PublishGround(): the painted ground, or the world's tiles), then a shadow under
    /// each entity that casts one, then the world's entities in foot-y order, then the effects, packed in fixed chunks on
    /// `jobs`: the bytes are the same whatever its worker count. Each entity's instance carries its ID, but a dying one's
    /// carries none and dissolves (core::SpriteDraw::dissolve, DissolveAt() at `snapshot.simulationTick`), its shadow with
    /// it; the sprites a scene's traits list (SceneTraits::demoGlows) wear their demo glows (DemoGlow()). What is drawn is
    /// culled to `view` (the main thread's window and its margin) or, with none yet, to the widest view a window shows
    /// (core::WIDEST_HALF_VIEW) round where the camera starts: the player's cultivator, which is the one to follow, or else
    /// the centre of the scene's view (SceneTraits::view); the painted ground is drawn whole either way, and the scene's
    /// hooks get the same area. The snapshot names the scene's camera (SceneTraits::camera). A scene with a player adds the
    /// player's and each living fox's name label over its head; a combat scene adds each living fox's health bar at its
    /// head and the player's casting state (core::Snapshot::caster). Then the scene adds its own (Scene::Publish(): the
    /// tablet's caption, the arena's showcase seals, the world's walker labels), then a scene with a portal adds its seal
    /// (PORTAL_SEAL at SceneTraits::portal, twice PORTAL_RADIUS in size, from tick 0 and never ending, no showcase seal, so
    /// F8 never hides it) and its "Portal" caption over it, both whatever the view, as the seals are: one record each, whose
    /// look reaches past its point. After them come the volleys in flight with their targets and the seals raised in play
    /// (Combat::Publish()).
    void Publish(core::Snapshot& snapshot, core::Jobs& jobs, const std::optional<core::Rect>& view) override;

private:
    template<typename Archive>
    void Write(Archive& archive) const;

    class Context; // the SceneContext a scene's hooks reach the host through

    [[nodiscard]] const game::Scene& Running() const {
        return SceneOf(m_scene);
    }
    void Build(core::SceneKey scene);
    [[nodiscard]] core::SceneFacts FactsOf(core::SceneKey scene) const;
    core::Entity Spawn(core::Vec2 foot, const Look& look);
    core::Entity SpawnFox(const Orbit& orbit, core::Vec2 foot);
    core::Entity SpawnPlayer(core::Vec2 foot);
    void Walk(core::Vec2 move);
    void Select(core::StableId target);
    void CastAction(const core::Action& action, const core::TickCounters& counters, std::vector<core::Event>& events);
    void DebugBurst(const core::TickCounters& counters, std::vector<core::Event>& events) const;
    void Leave(core::SceneKey next, std::vector<core::Event>& events);
    [[nodiscard]] bool OnPortal() const;
    void PublishPortal(core::Snapshot& snapshot) const;
    void PublishOverhead(core::Snapshot& snapshot) const;
    void PublishCaster(core::Snapshot& snapshot) const;
    void ApplyData();
    [[nodiscard]] const core::CreatureData& DataOf(CreatureKind kind) const {
        return kind == CreatureKind::Fox ? m_data.fox : m_data.cultivator;
    }

    // NOLINTBEGIN(readability-redundant-member-init): GCC's -Weffc++ requires an initializer for
    // members whose default constructor is defaulted.
    core::DrawList m_drawList{};
    Relationships m_relationships{};
    MonthStep m_monthStep{};
    Combat m_combat{};
    // NOLINTEND(readability-redundant-member-init)
    std::shared_ptr<const core::Manifest> m_manifest;
    core::State m_state;
    core::GameData m_data;
    core::SceneKey m_scene;
    core::SceneId m_sceneId;     // m_scene's in the manifest
    u64 m_months = 0;            // the calendar: months passed
    core::StableId m_player{};   // the cultivator the player walks; zero for none
    core::StableId m_selected{}; // zero for none

    // The scene a CycleScene asked for, until it is entered.
    std::optional<core::SceneKey> m_requested = std::nullopt;

    // A sound load's state, held until EnterScene() makes it current. A constructor, not an aggregate: core::State has no
    // default, and MSVC warns (C4623) where an aggregate's default constructor is implicitly deleted.
    struct Loaded {
        Loaded(
            core::State loadedState,
            Relationships loadedRelationships,
            Combat loadedCombat,
            core::SceneKey loadedScene,
            u64 loadedMonths,
            core::StableId loadedPlayer,
            core::StableId loadedSelected
        )
          : state(std::move(loadedState))
          , relationships(std::move(loadedRelationships))
          , combat(std::move(loadedCombat))
          , scene(loadedScene)
          , months(loadedMonths)
          , player(loadedPlayer)
          , selected(loadedSelected) {
        }

        core::State state;
        Relationships relationships;
        Combat combat;
        core::SceneKey scene;
        u64 months;
        core::StableId player;
        core::StableId selected;
    };
    std::optional<Loaded> m_loaded = std::nullopt;
};

} // namespace game
