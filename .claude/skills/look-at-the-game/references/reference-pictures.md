# Reference pictures: judging drift and updating them

`pixi run project_n reference` (Linux; part of `check` and `artifact`) draws fixed scenes on Mesa's software Vulkan and
compares each with the committed PNG in `tests/reference/`. A run writes its pictures to `build/reference/<view>.png`; the
views are the `.name` of each `TEST_CASE` in `tests/reference/reference_tests.cpp`, whose name and comment say what the
picture shows (`ls tests/reference/` lists them). Open them with the Read tool. They match CI, not necessarily the user's GPU.

## When a change needs `reference --update`
A change to shaders, sprites or a scene's layout that moves pixels. Run `pixi run project_n reference --update`, look at
every new picture in `tests/reference/` (the failing run's are in `build/reference/`), and commit the PNGs in the same
commit as the change. `--update` refuses to run when a committed view was not redrawn.

## Pictures your change did not touch
If `--update` rewrites pictures your change did not touch, they had drifted, not changed between runs: lavapipe draws every
case the same to the byte, run after run, at any thread count, from GCC and Clang. A change that moves pixels only within
`TOLERANCE` passes, so nobody refreshed its pictures. A passing `reference` names them: "within tolerance but changed".
Renderer changes and atlas re-packs did this (an explicit atlas gutter, placeholder seal runes). Reverting such a picture by
hand keeps it stale for every later update: look at it and commit it with the change that moved it.

## Atlas changes
Appending a sprite to `art/sprites.toml` moves no other sprite (`pack_atlas` places sprites in list order). A resized or
reordered sprite still shifts its pixels a few levels at zoom 1 and 2 (its exact position), though not its mips
(`ATLAS_ALIGN`): look at such pictures and commit them with the change that moved them.
