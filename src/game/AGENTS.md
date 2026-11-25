# src/game/: the game's rules and state

## Rules
- The arena (`game/scenes/arena.hpp`) is the growing demo scene: a new game or engine feature shows up in it, in the same branch, and joins the header's list.
- A change to what `GenerateWorld` makes raises `GENERATOR_VERSION` → `game/world_state.hpp`.
- Presentation-only host state (walk tables, kept walks, the player's walk, the drawn shift, `FogVersion`, the debug sight) is never saved, hashed or read by the simulation → `game/host.hpp`.
- Travel, journeys and month steps: every random draw is keyed on the seed, state counter, month and NPC, so a run is the same on any worker count → `game/travel.hpp`.
- The world map's walking and fog live in `core/journey.hpp`, `core/leg_walk_cache.hpp`, `core/road_curve.hpp`, `core/fog.hpp` and `core/calendar.hpp`; the world scene is `game/scenes/world.hpp`.
