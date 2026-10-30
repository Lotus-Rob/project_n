---
name: add-dependency
description: Add, update or remove a C++ library dependency in project_n. Use when adding a library or package, calling add_requires or add_packages, bumping a package version, changing xmake-requires.lock, or when a new package fails to build, links a host library or loses a feature.
---

# Adding a C++ dependency

Owned by: root AGENTS.md. Rationale: docs/design/0001 §5.2 (linking), §6.1 (pinning), §7.1.

## Steps

1. Find the package in xmake-repo at the commit recorded in `xmake-requires.lock`; pick an exact version that it offers. Check the licence: it must allow static linking into a closed game. Ask the user about anything copyleft (GPL, LGPL static linking).
2. In `xmake.lua`, add `add_requires("<name> <x.y.z>")` with the licence in a trailing comment (`-- zlib`; add `tests only` when it never ships). These comments are the project's licence record. Add `add_packages("<name>")` only to the targets that use it.
3. `pixi run project_n build --preset linux-clang`, then `--preset windows-msvc` (container). Each one writes its platform into `xmake-requires.lock`; check that the lock now has entries for both `linux|x86_64` and `windows|x64`.
4. Check that nothing came from the host. Read the package's configure output for features that were silently skipped: SDL3 dropped Wayland without failing. If the package only builds with host files, fix it with an override in `xmake/repo/` (see the existing two, which use `set_base`) and tell the user it is a `fix-xmake-package` candidate.
5. If the package needs a build tool (cmake, meson, autotools…), the tool must come pinned from `pixi.toml` and be listed in the `add_requireconfs("**|...", {system = false})` exceptions in `xmake.lua`.
6. `pixi run project_n check`, `pixi run project_n check --container`, and `pixi run project_n artifact --preset linux-gcc` (artifact fails if the binary depends on a shared library from the Pixi environment).
7. Commit `xmake.lua` with `xmake-requires.lock`: `build(deps): add <name> <x.y.z>`.

## Linking rules (Linux)

- Shipped binaries need only glibc 2.28 plus common system libraries. Packages link statically (XMake's default); never ship a `.so` from the Pixi environment.
- Libraries that talk to the desktop (X11, Wayland, GL, Vulkan, audio) are loaded by SDL at runtime from the player's system; don't link them directly.
- Sanitizers apply only to our targets, never packages (see the `sanitizer` rule in xmake.lua).

## Updating or removing

Same steps: change or delete the `add_requires` line, rebuild both platforms so the lock follows, and commit as `build(deps): bump <name> to <x.y.z>` or `build(deps): remove <name>`.

## Gotchas
- Never pipe `project_n build` output while packages install: XMake can stall (tools/AGENTS.md).
