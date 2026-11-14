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
/// Its stress variant (game/scenes/stress.hpp) adds a crowd and a bullet spiral, about 100,000 instances in all (§8.1),
/// and a world tier of 50,000.

#include "core/frame.hpp"
#include "core/tick_clock.hpp"
#include "core/types.hpp"
#include "game/scene.hpp"

#include <vector>

namespace game {

/// The demonstration arena: a combat scene the camera follows the player round.
class ArenaScene final : public SceneClass {
public:
    /// Named `arena`, viewing the ground, a combat scene with creatures and the demo glows (ARENA_DEMO_GLOWS), the camera
    /// following the player, with 5,000 world NPCs.
    [[nodiscard]] const SceneTraits& Traits() const override;

    /// The shared layout (game/scenes/arena_layout.hpp) and its walking NPCs.
    void Build(SceneContext& context) const override;

    /// Every `auraPeriod`th simulation tick (core::CombatRules), the crystal's aura hits each living fox in its reach and
    /// heals the player if in reach, with a hit event for each, then ends its use with a UseEnded event.
    void Tick(SceneContext& context, const core::TickCounters& counters, std::vector<core::Event>& events) const override;

    /// A fox at the ground's edge (RespawnFox()).
    void Respawn(SceneContext& context, const core::TickCounters& counters, u32 index) const override;

    /// The tablet's caption while there is a player (PublishCaption()), then the showcase seals (core::Seal::showcase,
    /// from tick 0 and never ending).
    void Publish(const SceneContext& context, core::Snapshot& snapshot) const override;
};

} // namespace game
