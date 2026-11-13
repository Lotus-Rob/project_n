#pragma once

/// @file
/// @ingroup game
/// The demonstration arena (design doc 0002 §2, 0002.3 §2): the growing demo scene, which shows every feature of the
/// game and engine. Each new feature adds itself here. So far:
/// - a painted ground with props and the painted art, in foot-y order, some drawn larger or smaller (scale) and one
///   turning (rotation), with blob shadows;
/// - the player's cultivator, walked by held input, which the camera follows and the logic culls around;
/// - a pack of foxes circling it wherever it walks and glowing orbs circling its start (orbits, the additive pipeline), the
///   foxes and the cultivator from the creatures data table (hot reload in debug builds);
/// - click-to-select, the selected entity outlined in gold and the hovered one in pale white;
/// - NPCs walking in the active tier and 5,000 in the world tier, whose months pass on request (§7.7);
/// - saves, loads and replays of all of it;
/// - F2 cycling through the scenes, each loaded on demand while the ticks wait for it;
/// - the ice crystal's aura, which hits the foxes in its reach (and heals the player) once a second, and F7's debug burst
///   of 4,000 hits: both emit hit events (core::Event), which the main thread shows as damage numbers; the aura's also
///   take the foxes' health (and heal the player's), while the burst's are numbers only;
/// - health (game/combat.hpp): a fox whose health runs out dissolves in blocky pixels, is removed, and another
///   appears at the ground's edge a few seconds later, easing into its orbit; each living fox reports its health
///   over its head (core::HealthBar);
/// - Qi and cooldowns (game/combat.hpp): a cast spends or gains the player's Qi and cools its slot down; the dash moves
///   the player;
/// - the abilities, cast at the world point under the cursor in this combat scene (the loadout and every number from
///   the abilities data table): the flying swords and the fire bolt (game/combat.hpp), volleys that tag the foxes
///   round the point, follow them, move on from the dead and strike at their scheduled ticks, the swords formed in
///   still rings, each a sunburst that holds, then flies; the bolt's hit leaves a burn that ticks; the main thread draws them
///   at the render time, each wearing its glow, with additive trails, and the foxes' health bars (gfx/volley_draw.hpp);
///   `--cast-swords` casts the swords on a timer for a profile run;
/// - the poison lance (game/combat.hpp): a strike that hits and poisons every fox round the point at once, its burst a
///   flash the main thread draws in the poison's colour; and the temporary HUD (app/hud.hpp) of the player's health,
///   Qi and slots, from the caster state the arena publishes;
/// - game text over the world: name labels over the player and every fox, and the tablet's vertical caption;
/// - the glow drives (core/glow.hpp), one demo prop each: the shrine pulses, the stele charges and releases on a loop, the
///   ice crystal fills and shows a result that changes each loop (DemoGlow());
/// - seals (core/seal.hpp, their looks in `assets/data/seals.yaml`): the flying swords raise one under their rings, and the
///   arena scene shows a showcase that between them uses every part and placement, each in a clearing of its own away
///   from the fight: a large ground seal of 9 rings, an upright one with satellite seals on its ring, one overhead with
///   pillars of light falling into its shadow on the ground and sparks rising, one in tiers of square and diamond frames,
///   one with flames spilling past its rings and drifting shards, and small ones of 1 and 3 rings; scene content,
///   republished every tick and never saved, which F8 hides and shows.
/// Its stress variant adds a crowd and a bullet spiral, about 100,000 instances in all (§8.1), and a world tier of 50,000.

#include "core/draw_list.hpp"
#include "core/ecs.hpp"
#include "core/error.hpp"
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

#include <cmath>
#include <expected>
#include <memory>
#include <optional>
#include <stop_token>
#include <string_view>
#include <vector>

namespace game {

/// A path around a point: the entity's foot circles `centre` at `radius`, a little in and out.
struct Orbit {
    /// The point circled.
    core::Vec2 centre{};
    /// The mean distance from it.
    f32 radius = 0.0F;
    /// How far it strays in and out of that distance, three times a lap.
    f32 wobble = 0.0F;
    /// The angle at tick 0, in radians.
    f32 phase = 0.0F;
    /// Radians per tick; negative goes anticlockwise.
    f32 speed = 0.0F;

    /// Lists its fields for an archive (core/archive.hpp).
    template<typename Self, typename Archive>
    void Fields(this Self& self, Archive& archive) {
        archive(self.centre, self.radius, self.wobble, self.phase, self.speed);
    }

    /// Whether a value read from a save can be used: finite numbers.
    [[nodiscard]] bool Valid() const {
        return core::IsFinite(centre) && std::isfinite(radius) && std::isfinite(wobble) && std::isfinite(phase) &&
               std::isfinite(speed);
    }
};

/// Where an Orbit puts its entity's foot at `tick`.
[[nodiscard]] core::Vec2 OrbitPoint(const Orbit& orbit, u64 tick);

/// The test bed's game data as built in: what `assets/data/creatures.yaml` ships, for runs that read
/// no data files (tests, tools).
[[nodiscard]] core::GameData BuiltInData();

/// Which arena to build.
enum class Scene : u8 {
    Arena,   ///< The demonstration arena.
    Stress,  ///< The arena with a crowd of 90,000, a spiral of 10,000 bullets and 50,000 world NPCs, for profiling.
    Painted, ///< A still scene of painted sprites and overlapping glows, for judging art and blending (§8.4).
    Cells,   ///< A still grid of 640 map cells, for judging the mips as the view zooms out (§16 phase 3).
};

/// The scene called `name` (SceneName()), if there is one.
[[nodiscard]] std::optional<Scene> SceneNamed(std::string_view name);

/// The name of `scene`'s sprites in the atlas manifest (art/scenes.toml): `arena`, `stress`, `painted`, `cells`.
[[nodiscard]] std::string_view SceneName(Scene scene);

/// Whether every scene can run from `manifest` with `data` (design doc 0002.3.1 §3): the manifest names each,
/// and the scenes with creatures load the sprites `data` gives them, without which they would be invisible.
/// Otherwise, which scene is short of what.
[[nodiscard]] std::expected<void, core::Error> CheckScenes(const core::Manifest& manifest, const core::GameData& data);

/// The input of the demo glow `role` at simulation `tick` (the shrine's pulse, the stele's charge on a 3.2 s loop that rises,
/// holds and releases, the crystal's progress on a 5 s loop that fills and then shows a result): pure and presentation only.
/// Its previous value is the one at `tick - 1` in the same loop, 0 at a loop's first tick. Any other role gives no glow.
[[nodiscard]] core::GlowInput DemoGlow(core::GlowRole role, u64 tick);

/// The arena scene as a simulation: it moves its creatures each tick and publishes every sprite.
class Arena final : public core::Simulation {
public:
    /// Builds `scene` with `data`'s creatures: the same entities, in the same places, every time. It
    /// draws its sprites where `manifest` puts them; `manifest` must pass CheckScenes().
    explicit Arena(
        std::shared_ptr<const core::Manifest> manifest,
        game::Scene scene = game::Scene::Arena,
        const core::GameData& data = BuiltInData()
    );

    /// Rebuilds itself as the next scene in Scene's order (arena, stress, painted, cells, then the arena again) when
    /// `input` holds a CycleScene, the selection cleared; walks the player's cultivator where `input`'s held
    /// movement points, selects what its Select actions name (nothing for a dying one), ignores Cast actions outside a combat
    /// scene (in one a Cast is Combat::Cast(): Qi, cooldown, the ability's effect; a dash moves the player in place of held
    /// movement), moves every entity on an Orbit to where it is at `counters.simulation`, advances combat (Combat::Tick():
    /// arrivals, removing the dead, which clears their selection, and spawning the foxes due at the ground's edge), and walks
    /// the active NPCs, whose decisions draw random numbers keyed on `counters.state`. In the arena scene, every 60th
    /// simulation tick the crystal's aura hits each living fox in reach and heals the player if in reach, appending a hit event
    /// for each; a DebugBurst action appends 4,000 hits round the foxes, which change nothing. Last, the volleys and
    /// afflictions strike (Combat::Strike()), appending their hits. Each use ends with a UseEnded event; a scene change ends
    /// every use still open first (Combat::EndUses()). The events themselves change nothing in the state.
    void Tick(const core::TickCounters& counters, const core::TickInput& input, std::vector<core::Event>& events) override;

    /// Lets a month pass for the world NPCs (MonthStep).
    void AdvanceCalendar(const core::TickCounters& counters, core::Jobs& jobs, const std::stop_token& stop) override;

    /// Sorts the state into canonical order and writes it, with the scene, the months passed and
    /// the relationships, into `archive`.
    void Save(core::Writer& archive) override;

    /// Replaces the state with the one Save() wrote at simulation tick `simulationTick`, its creatures taking the game
    /// data in use; false, changing nothing, when the bytes are malformed, of another version, don't end where the state
    /// does, or hold a flash or a seal that starts after `simulationTick`.
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

    /// How many months have passed.
    [[nodiscard]] u64 Months() const {
        return m_months;
    }

    /// The game state: the arena's entities in the active domain, NPCs in the world domain.
    [[nodiscard]] const core::State& State() const {
        return m_state;
    }

    /// Whether the scene is a combat scene, where fights happen and casts act: the arena only.
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

    /// Publishes the ground, then a shadow under each entity that casts one, then the world's
    /// entities in foot-y order, then the effects, packed in fixed chunks
    /// on `jobs`: the bytes are the same whatever its worker count. Each entity's instance carries
    /// its ID, but a dying one's carries none and dissolves (core::SpriteDraw::dissolve, DissolveAt() at
    /// `snapshot.simulationTick`), its shadow with it. The player's cultivator is the one to follow, with what is drawn
    /// culled around it. A scene with a player adds its texts: the player's and each living fox's name label over its
    /// head, and the tablet's caption; the arena scene adds each living fox's health bar at its head, the player's casting
    /// state (core::Snapshot::caster), its showcase seals (core::Seal::showcase, from tick 0 and never ending) and, after
    /// them, the volleys in flight with their targets and the seals raised in play (Combat::Publish()).
    void Publish(core::Snapshot& snapshot, core::Jobs& jobs) override;

private:
    template<typename Archive>
    void Write(Archive& archive) const;

    void Build(game::Scene scene);
    core::Entity Spawn(core::Vec2 foot, const Look& look);
    void Walk(core::Vec2 move);
    void Select(core::StableId target);
    void CastAction(const core::Action& action, const core::TickCounters& counters, std::vector<core::Event>& events);
    void Aura(const core::TickCounters& counters, std::vector<core::Event>& events);
    void DebugBurst(const core::TickCounters& counters, std::vector<core::Event>& events) const;
    [[nodiscard]] core::Vec2 HeadOf(core::Entity entity) const;
    void PublishOverhead(core::Snapshot& snapshot) const;
    void PublishCaster(core::Snapshot& snapshot) const;
    core::Entity SpawnFox(const Orbit& orbit, core::Vec2 foot);
    void Respawn(const core::TickCounters& counters, u32 index);
    void ApplyData();
    [[nodiscard]] const core::CreatureData& DataOf(CreatureKind kind) const {
        return kind == CreatureKind::Fox ? m_data.fox : m_data.cultivator;
    }
    void SpawnStress();
    void SpawnNpcs(u32 worldNpcs);
    void SpawnPainted();
    void SpawnCells();

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
    game::Scene m_scene;
    core::SceneId m_sceneId;     // m_scene's in the manifest
    u64 m_months = 0;            // the calendar: months passed
    core::Rect m_view{};         // what a publish culls to without a player
    core::StableId m_player{};   // the cultivator the player walks; zero for none
    core::StableId m_selected{}; // zero for none
};

} // namespace game
