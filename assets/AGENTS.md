# assets/: what the game loads at runtime

## Map
- `sprites/`: images, with `PROVENANCE.md` · `fonts/`: the font, with its licence and `PROVENANCE.md` · `manifest.yaml`: the atlas, which `atlas` writes
- `data/`: YAML data tables, game data (hashed) or presentation (not hashed); debug builds reload `effects.yaml`, `seals.yaml`, `world_look.yaml`, `creatures.yaml` and `abilities.yaml` when they change (`App::WatchData`), and read the others once at start. Each is read into, or mirrored by, C++:
  - `creatures.yaml` and `abilities.yaml` (with its motion and `combat:` rules): game data, hashed; `game::BuiltInData()` mirrors them
  - `effects.yaml`: presentation, not hashed, hot-reloaded: the effects' looks and the HUD's layout; `core::Effects{}` mirrors it
  - `seals.yaml`: presentation, not hashed, hot-reloaded: each seal's look, read into `core::Seals`
  - `world_look.yaml`: presentation, not hashed, hot-reloaded: each map kind's wash and painted stamps (`kinds:`, at most 16), the road overlays, bridges, fords and markers, the route line and target cursor, the fog's clouds and colours (`fog:`), and the paper, veil, mist, river ribbon and place paintings (`ground:`, `veil:`, `mist:`, `river:`, `places:`), every section required; `data::LoadWorldLook` into `core::WorldLook`, checked against the map
  - `world.yaml`: game data, hashed, read at start after the strings, not reloaded: the world map, `data::LoadWorldMap` into `core::GameData::world`; `art/world_map.py` generated it once and it is hand-tuned since (art/AGENTS.md)
  - `names.yaml`: game data, hashed, read at start after the map, not reloaded: the surnames and given characters NPC names are drawn from, `data::LoadNames` into `core::GameData::names`
  - `strings.yaml`: the text in each language; not game data, so not hashed, and read once at start
