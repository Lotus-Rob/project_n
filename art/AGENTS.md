# art/: where the sprites and the world map come from

## Map
- `sprites.toml`: the sprite list, with foot points; a sprite's place in it is its ID, which may change (sprites reordered or removed) until release 1.0.0 and never after · `scenes.toml`: each scene's sprites
- `placeholders.py`: procedural art; the world map's painted marks and the 8-frame walk cycles, their margins and strides → its module docstring; an artist's PNG replacing one needs its margins checked by hand
- `atlas.sha256`: written by `atlas` with the atlas, so `lint` can tell a stale one
- `world_map.py` (run it with `pixi run python`): generates `assets/data/world.yaml` and previews in `build/world_map_*.png`; the YAML is hand-tuned after and is the source from then on, so a rerun starts over → its docstring
- `world_map.py --roads` reroutes only the `roads:` and `fords:` blocks, keeping every other line; it refuses a comment inside them → its docstring
- After a hand edit to `world.yaml`'s grid, rivers, places, `danger_reach` or the power-0 profile, run `world_map.py --roads` (with `pixi run python`) and commit its `roads:`/`fords:`; `selftest` fails on stale roads
- The generator doesn't know where a river may rise, so each river's source was placed by hand in `world.yaml`, and a rerun loses them → `art/world_map.py`'s docstring, assets/AGENTS.md
