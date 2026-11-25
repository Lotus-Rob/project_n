# src/gfx/: drawing

## Rules
- The world pass's draw order and each layer's job are in `gfx/module.dox` ("Draw order"); a new layer goes in that list.
- Nothing outside `gfx` calls SDL_GPU.
- Shaders are HLSL in `shaders/<name>.<stage>.hlsl`; after changing one, `./dev shaders` (Linux or Windows) and commit `shaders/bin/` and `src/gfx/generated/` with it (`lint` fails on stale binaries).
- `gfx` reaches the compiled shaders through the generated `gfx/generated/shaders.hpp`.
