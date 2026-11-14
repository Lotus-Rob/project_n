# Sprite provenance

Where every image in this folder came from. Replacing one (an AI-generated image, a painting)
updates its row in the same commit (what made it, when, and who) and takes it out of
`PLACEHOLDERS` in `art/placeholders.py`, so `./dev atlas --placeholders` never paints over it. The pipeline's contract is in
`art/sprites.toml`; after any change, `./dev atlas` and commit what it writes with the images.

| Image | Made by | Date | Notes |
|---|---|---|---|
| ground.png | `art/placeholders.py` (procedural placeholder) | 2026-11-01 | original; `./dev atlas --placeholders` redraws it byte for byte |
| rock_large.png | `art/placeholders.py` (procedural placeholder) | 2026-11-01 | as above |
| rock_small.png | `art/placeholders.py` (procedural placeholder) | 2026-11-01 | as above |
| pine.png | `art/placeholders.py` (procedural placeholder) | 2026-11-01 | as above |
| shrine.png | `art/placeholders.py` (procedural placeholder) | 2026-11-01 | as above |
| fox.png | `art/placeholders.py` (procedural placeholder) | 2026-11-01 | as above |
| cultivator.png | `art/placeholders.py` (procedural placeholder) | 2026-11-01 | as above |
| orb_blue.png | `art/placeholders.py` (procedural placeholder) | 2026-11-01 | as above |
| orb_red.png | `art/placeholders.py` (procedural placeholder) | 2026-11-01 | as above |
| shadow.png | `art/placeholders.py` (procedural placeholder) | 2026-11-01 | as above |
| cell_plain.png | `art/placeholders.py` (procedural placeholder) | 2026-11-01 | as above |
| cell_mountain.png | `art/placeholders.py` (procedural placeholder) | 2026-11-01 | as above |
| cell_forest.png | `art/placeholders.py` (procedural placeholder) | 2026-11-01 | as above |
| cell_city.png | `art/placeholders.py` (procedural placeholder) | 2026-11-01 | as above |
| treant_blighted.png | AI image generation (`openrouter/sourceful/riverflow-v2.6-pro`) via Claude | 2026-11-02 | test sprite, Tale of Immortal-inspired ink wash; generated on plain white, keyed to straight alpha and resized; not in `PLACEHOLDERS` |
| wisp_red.png | AI image generation (`openrouter/sourceful/riverflow-v2.6-pro`) via Claude | 2026-11-02 | as above |
| crane.png | AI image generation (`openrouter/sourceful/riverflow-v2.6-pro`) via Claude | 2026-11-02 | as above |
| tree_dead.png | AI image generation (`openrouter/sourceful/riverflow-v2.6-pro`) via Claude | 2026-11-02 | as above |
| fox_spirit.png | AI image generation (`openrouter/sourceful/riverflow-v2.6-pro`) via Claude | 2026-11-02 | as above |
| fox_yellow.png | AI image generation (`openrouter/sourceful/riverflow-v2.6-pro`) via Claude | 2026-11-02 | as above |
| hut.png | AI image generation (`openrouter/sourceful/riverflow-v2.6-pro`) via Claude | 2026-11-02 | as above |
| stele.png | AI image generation (`openrouter/sourceful/riverflow-v2.6-pro`) via Claude | 2026-11-02 | as above |
| crystal_ice.png | AI image generation (`openrouter/sourceful/riverflow-v2.6-pro`) via Claude | 2026-11-02 | as above |
| cell_snow.png | AI image generation (`openrouter/sourceful/riverflow-v2.6-pro`) via Claude | 2026-11-02 | as above; opaque terrain tile, no keying |
| orb_gold.png | AI image generation (`openrouter/sourceful/riverflow-v2.6-pro`) via Claude | 2026-11-02 | as above |
| villager.png | `art/placeholders.py` (procedural placeholder) | 2026-11-07 | the NPCs' sprite; `./dev atlas --placeholders` redraws it byte for byte |
| sword.png | `art/placeholders.py` (procedural placeholder) | 2026-11-11 | the flying swords; `./dev atlas --placeholders` redraws it byte for byte |
| halo.png | `art/placeholders.py` (procedural placeholder) | 2026-11-11 | white; unused since the swords wear glows, kept for its stable ID; as above |
| fire_bolt.png | `art/placeholders.py` (procedural placeholder) | 2026-11-11 | white, tinted by its element's colour; as above |
| lance_strike.png | `art/placeholders.py` (procedural placeholder) | 2026-11-11 | white, tinted by its element's colour; as above |
| solid.png | `art/placeholders.py` (procedural placeholder) | 2026-11-11 | the bars' white square; as above |
| glow_core.png | `art/placeholders.py` (procedural placeholder) | 2026-11-11 | white; unused since the swords wear glows, kept for its stable ID; as above |
| rune_00.png to rune_23.png | `art/placeholders.py` (procedural placeholder) | 2026-11-13 | the seals' invented runes, 2 to 5 strokes on a 3 x 4 grid; white, tinted by the seal; `./dev atlas --placeholders` redraws them byte for byte |
| flourish_flame_0.png to flourish_flame_2.png | `art/placeholders.py` (procedural placeholder) | 2026-11-13 | the seals' flame flourishes, 256 x 128; white; as above |
| emblem_lotus.png, emblem_swords.png, emblem_eye.png, emblem_spiral.png | `art/placeholders.py` (procedural placeholder) | 2026-11-13 | the seals' emblems, 256 x 256, abstract and original; white; as above |
| terrain_meadow_0.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | the world's terrain, ten kinds with two variants each; opaque 256 x 256, seamless with itself and its other variant; `./dev atlas --placeholders` redraws it byte for byte |
| terrain_meadow_1.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
| terrain_grass_0.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
| terrain_grass_1.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
| terrain_forest_0.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
| terrain_forest_1.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
| terrain_hills_0.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
| terrain_hills_1.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
| terrain_rock_0.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
| terrain_rock_1.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
| terrain_snow_0.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
| terrain_snow_1.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
| terrain_sand_0.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
| terrain_sand_1.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
| terrain_marsh_0.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
| terrain_marsh_1.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
| terrain_shallows_0.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
| terrain_shallows_1.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
| terrain_water_0.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
| terrain_water_1.png | `art/placeholders.py` (procedural placeholder) | 2026-11-14 | as above |
