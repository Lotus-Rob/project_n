# art/: where the sprites and the world map come from

## Map
- `sprites.toml`: the sprite list, with foot points; a sprite's place in it is its ID, which may change (sprites reordered or removed) until release 1.0.0 and never after · `scenes.toml`: each scene's sprites · `placeholders.py`: procedural art, with the world map's road overlays, crossings and markers and its painted marks (`stamp_<kind>_0..3`, `mist_0..3`, `painting_*`), each drawn on a transparent sheet with its foot at the bottom centre, a stamp's art kept about `STAMP_MARGIN` texels off its sides (the far zoom draws it from small mips, and the quad's edge cutting their blur crawls) · `atlas.sha256`: written by `atlas` with the atlas, so `lint` can tell a stale one
- `world_map.py` (`pixi run python art/world_map.py`): generates `assets/data/world.yaml` from land forming, previews in `build/world_map_*.png`; the YAML is hand-tuned after and is the source from then on, so a rerun starts over
