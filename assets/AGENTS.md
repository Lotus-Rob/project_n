# assets/: what the game loads at runtime

## Map
- `sprites/`: images, with `PROVENANCE.md` · `fonts/`: the font, with its licence and `PROVENANCE.md` · `manifest.yaml`: the atlas, which `atlas` writes
- `data/`: YAML data tables, game data (hashed) or presentation (not hashed); debug builds reload `effects`, `seals`, `animations`, `world_look`, `creatures` and `abilities` `.yaml` when they change (`App::WatchData`), the others once at start. Each is read into, or mirrored by, C++:
  - `creatures.yaml` and `abilities.yaml` (with its motion and `combat:` rules): game data, hashed; `game::BuiltInData()` mirrors them
  - `effects.yaml`: presentation, not hashed, hot-reloaded: the effects' looks and the HUD's layout; `core::Effects{}` mirrors it
  - `seals.yaml`: presentation, not hashed, hot-reloaded: each seal's look, read into `core::Seals`
  - `animations.yaml`: presentation, not hashed, hot-reloaded: each figure's walk clip (its frames and stride, keyed by its standing sprite), read into `core::Animations` and sent to the logic thread in `ViewReport::animations`
  - `world_look.yaml`: presentation, not hashed, hot-reloaded: the world map's look, every section required; `data::LoadWorldLook` into `core::WorldLook`, checked against the map → `src/data/world_look.hpp`
  - `world.yaml`: game data, hashed, read at start after the strings, not reloaded: the world map, `data::LoadWorldMap` into `core::GameData::world`; hand-tuned after `art/world_map.py` generated it, but for its `roads:` and `fords:` (art/AGENTS.md)
    - A river in `world.yaml` must rise somewhere: on high ground, out of water or from another river; `gfx::RiverStarts` judges it as the renderer draws it, and the gfx test fails on one that doesn't → `src/gfx/river_draw.hpp`
  - `names.yaml`: game data, hashed, read at start after the map, not reloaded: the surnames and given characters NPC names are drawn from, `data::LoadNames` into `core::GameData::names`
  - `strings.yaml`: the text in each language; not game data, so not hashed, and read once at start
