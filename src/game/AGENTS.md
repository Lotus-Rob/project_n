# src/game/: the game's rules and state

## Rules
- A change to what `GenerateWorld` makes raises `GENERATOR_VERSION` → `game/world_state.hpp`.
- Presentation-only host state (walk tables, kept walks, the player's walk, the drawn shift, `FogVersion`, the debug sight) is never saved, hashed or read by the simulation → `game/host.hpp`.
- Travel, journeys and month steps: every random draw is keyed on the seed, state counter, month and NPC, so a run is the same on any worker count → `game/travel.hpp`.
- The world map's walking and fog live in `core/journey.hpp`, `core/leg_walk_cache.hpp`, `core/road_curve.hpp`, `core/fog.hpp` and `core/calendar.hpp`; the world scene is `game/scenes/world.hpp`.
- Scenes are stateless classes in `game/scenes/` behind `game/scene.hpp`, listed in `game/scenes.cpp` in F2's order → `game/scenes.hpp`. Rules for `gfx` and `game`: `gfx/AGENTS.md`, `game/AGENTS.md`.
- The host owns all state and never checks which scene it runs: the game's world outlives scene changes, a dev scene runs on a scratch world made afresh → `game/host.hpp`, `game/world_state.hpp`.
- A save holds a header (the scene, the session's scene-ID counter), then the world as its difference from its base (identity: seed, `GENERATOR_VERSION`, `BaseHash`), a scratch world, the scene; it never sorts, a load restores each order → `Host::Save`, `game/host.hpp`.
