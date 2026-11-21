# art/: where the sprites and the world map come from

## Map
- `sprites.toml`: the sprite list, with foot points · `scenes.toml`: each scene's sprites · `placeholders.py`: procedural art, with the world map's painted marks (`stamp_<kind>_0..3`, `mist_0..3`, `painting_*`), each drawn on a transparent sheet with its foot at the bottom centre · `atlas.sha256`: written by `atlas` with the atlas, so `lint` can tell a stale one
- `world_map.py` (`pixi run python art/world_map.py`): generates `assets/data/world.yaml` from land forming, previews in `build/world_map_*.png`; the YAML is hand-tuned after and is the source from then on, so a rerun starts over
