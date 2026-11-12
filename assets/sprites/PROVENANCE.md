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
