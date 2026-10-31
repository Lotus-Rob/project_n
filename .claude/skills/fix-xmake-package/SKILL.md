---
name: fix-xmake-package
description: Diagnose and fix an xmake-repo package in project_n. Use when a package builds natively but fails in the container, includes or links host files, silently drops a feature (as SDL3 dropped Wayland), needs an override in xmake/repo/, or when an override's upstream fix is prepared or has landed.
---

# Fixing an xmake-repo package

Owned by: root AGENTS.md. Rationale: docs/design/0001 §7.1 rules 5–7. The existing overrides in `xmake/repo/packages/` are worked examples.

## Symptoms

- Builds natively, fails in `pixi run project_n check --container`: a file on the dev machine filled a gap the package doesn't declare.
- Builds everywhere, but a feature is missing at runtime. SDL3 built without Wayland because two Wayland build dependencies were undeclared; nothing failed.
- A library resolves from the host (pacman, apt, pkg-config). Rule 5's `add_requireconfs` lockdown normally prevents this; if it happens, the lockdown is the bug.

## Diagnose

1. Reproduce in a clean container: `pixi run project_n test --preset <preset> --container`, or `pixi run project_n shell --preset <preset>` to look around. A native-only failure is not a reproduction.
2. Read the package's configure output. `project_n` runs `xmake f -v`, so it's in the build log; hand long logs to the `log-triage` agent. Look for "not found", "Could NOT find", "disabled" and feature summaries.
3. Compare what the library's own build needs (its CMake or meson options for the feature) with `add_deps` in xmake-repo's `packages/<letter>/<name>/xmake.lua`, at the xmake-repo commit recorded in `xmake-requires.lock`. The gap is the missing dependency.
4. For a feature that may have been dropped silently, check the result, not the build: the library's feature report, `strings` on the binary, or running it (SDL reports `SDL_GetCurrentVideoDriver()`).

## Fix

1. Add `xmake/repo/packages/<letter>/<name>-<what it fixes>/xmake.lua`, inheriting the original:

   ```lua
   -- What upstream lacks and why it matters. Upstream candidate for xmake-repo.
   package("<name>-<what it fixes>")
       set_base("<name>")
       on_load(function (package)
           package:base():script("load")(package) -- keep the base package's own load logic
           package:add("deps", "<missing>", {private = true}) -- private when only the build needs it
       end)
   ```

2. In `xmake.lua`, require the override in place of the original, with the exact version and the licence comment (add-dependency skill).
3. A dependency that is a build tool comes pinned from `pixi.toml` and joins the `add_requireconfs("**|...", {system = false})` exceptions.
4. `pixi run project_n check --container`; both platforms' entries in `xmake-requires.lock` follow. Commit `xmake/repo/`, `xmake.lua` and the lock as `build(deps): ...`.
5. Add the override to design doc 0001 §7.1 rule 7.

## Upstream

- Draft the same change against the original package in xmake-repo, with the evidence from Diagnose. Posting it is public and in the user's name: give the user the draft; never file it yourself.
- When the fix has landed and the lock's xmake-repo commit includes it, delete the override, require the original package again, and update rule 7.

## Gotchas

- Duplicate variants of one package with the same build hash corrupt each other when installed in parallel; `xmake f -v` serialises installs (tools/AGENTS.md). Don't drop `-v` while debugging a package.
