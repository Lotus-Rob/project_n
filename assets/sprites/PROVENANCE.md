# Sprite provenance

Where every image in this folder came from. Replacing one (an AI-generated image, a painting)
updates its row in the same commit (what made it, when, and who) and takes it out of
`PLACEHOLDERS` in `art/placeholders.py`, so `./dev atlas --placeholders` never paints over it. The pipeline's contract is in
`art/sprites.toml`; after any change, `./dev atlas` and commit `art/bin/` with the images.

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
