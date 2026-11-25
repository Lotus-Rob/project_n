# src/: game and engine

## Scope
All game and engine code, laid out by docs/design/0002-engine-architecture.md (§4 modules, §16 phases): XMake static libraries in `src/<module>/`, each linking only the modules below it (`MODULES` in tools/project_n.py; `lint` checks the includes).
Modules: `core` (frame contract, value types), `data` (reads the YAML; only it sees yaml-cpp), `platform`, `gfx`, `game` (links only `core`), `app` (puts the program together); each `module.dox` says what it holds. `main.cpp` reads the files and runs an `App`.
The YAML files, which are hashed game data, which presentation, and the C++ each is read into → `assets/AGENTS.md`. A new system is owned by `App` or `Game`, never passed in as loose arguments → `app/app.hpp`.

## Rules
- C++23 with conventional headers (no modules). Every translation unit's includes lead to `core/cxx23.hpp`, which fails the build below C++23; include it with `// IWYU pragma: keep`.
- Include order: project headers, then library headers (`<SDL3/...>`), then (headers only) the standard library, as blank-line-separated blocks. clang-format sorts within a block but doesn't regroup.
- Formatting and lint: `pixi run project_n fmt` and `pixi run project_n lint` (pinned tools; warnings are errors).
- Headers include only the standard library and our own headers, and forward-declare none of their types. Third-party and platform code (SDL, Tracy, `<windows.h>`) stays in `.cpp` files behind our own types (a pimpl or opaque handle); `lint` fails on a third-party include in a header.
- Only a facade (`FACADES` in tools/project_n.py) includes its own library in a header, and a module's private `src/<module>/internal/` headers its libraries (nothing outside the module includes them).
- Numbers use the sized types from `core/types.hpp` (`i8`–`i64`, `u8`–`u64`, `f32`, `f64`), never `int`, `unsigned`, `long`, `float`, `double` or `std::uint32_t` names (`int main(int, char**)` excepted; `std::size_t` stays for sizes and indices); `lint` fails on them. Headers include it themselves.
- `pch.hpp` is the game's precompiled header and holds every standard header the sources use (it includes `core/types.hpp`): a `.cpp` never includes a standard header itself (`lint` fails on one), so add it there. Headers still include what they use; no third-party ones in it.
- Logging is extensive, through `core/log.hpp`: log outcomes (what was chosen, happened, failed and why), never code steps or internals, never every frame (profile instead). Nothing else writes output or includes Quill or Tracy (`lint` fails on them). Levels, files, `Start` → `core/log.hpp`.
- Profiling: `PN_PROFILE_ZONE();` at the top of every function that runs each frame (not per-item ones), `PN_PROFILE_FRAME();` once per frame, from `core/profile.hpp` (Tracy; only `releasedbg` collects). Recording (`./dev profile`, `profiler`) and what skews a profile → `core/profile.hpp`.
- Every header starts with `/// @file` then `/// @ingroup <module>` (groups: `src/<module>/module.dox`), and every public class, function, member, variable and enum has a `///` comment: what it does and promises, not how. `lint` fails on a missing or broken one.
- Errors: an expected failure returns `std::expected<T, core::Error>` (`core/error.hpp`); a broken invariant is `PN_ASSERT(condition, message)` (`core/assert.hpp`: on in debug and releasedbg); we throw nothing.
- Entities: EnTT only via `core/ecs.hpp`. Across a domain, tick, save or input an entity is its `core::StableId`, never an EnTT handle. Two saved, never reused ID counters: the world's (from 1), the scene one (from `core::SCENE_ID_BASE`). No create or destroy in a job chunk → `core/state.hpp`.
- Simulation code (`core`, `game`) is deterministic within one binary (design doc 0002 §7.4): transcendental maths through `core/math.hpp` (`lint` fails on `std::sin` and the like there), random numbers through `core/random.hpp` keyed on the state counter, never `std::*_distribution`, never a clock.
- Comments in `.cpp` files only explain why, or a complexity the code can't show; never restate what the code does. No comment cites a spec, chunk, phase, plan, ruling, task or design doc (`lint` fails on one): say why in your words.
- A comment directly above a function, method, lambda-holding variable or helper type that states its contract (returns, inputs' ranges, promises) is `///` Doxygen; one that only restates the body goes; a comment on what a constant or data table holds is its documentation and stays.
- Third-party code comes from xmake-repo packages with exact versions; nothing is vendored into `src/`.
- `src/<module>/generated/` holds headers that `shaders` and `atlas` write (shader binaries, the sprite names; the manifest itself is `assets/manifest.yaml`): never edit them by hand; `fmt` and `lint` skip them, and `lint` fails when they are stale.
- Scenes are stateless classes in `game/scenes/` behind `game/scene.hpp`, listed in `game/scenes.cpp` in F2's order → `game/scenes.hpp`. Rules for `gfx` and `game`: `gfx/AGENTS.md`, `game/AGENTS.md`.
- The host owns all state and never checks which scene it runs: the game's world outlives scene changes, a dev scene runs on a scratch world made afresh → `game/host.hpp`, `game/world_state.hpp`.
- A save holds a header (the scene, the session's scene-ID counter), then the world as its difference from its base (identity: seed, `GENERATOR_VERSION`, `BaseHash`), a scratch world, the scene; it never sorts, a load restores each order → `Host::Save`, `game/host.hpp`.
- A new module adds its directory's `module.dox`, an XMake target with `add_deps` on the modules below it, and its commit scope to `SCOPES` in tools/project_n.py.

## Commands
- `./dev run [--debug] [-- <game flags>]` (native preset; `--preset` picks another). The flags, default bindings, debug keys and the debug data reload → `src/app/module.dox` ("Running the game").

## Skills
- look-at-the-game: seeing what the game draws (reference pictures, screenshots of the window, picture diffs)

## Gotchas
- A `///` comment right after the `/// @file` block joins it, leaving the next declaration undocumented: put a blank line after the file comment.
- A `switch` over every value of an enum fails either way under Clang's `-Weverything`: `-Wswitch-default` wants a `default`, `-Wcovered-switch-default` rejects one. Use `if`s.
- A member a designated initialiser can omit needs an initializer under Clang (`-Wmissing-designated-field-initializers`): `{}` with `core/frame.hpp`'s `NOLINTNEXTLINE(readability-redundant-member-init)`, or a value tidy accepts (`= nullptr`, `= std::nullopt`). Others stay bare.
- MSVC's warnings show only in the windows-msvc builds (each task's release build, `check`), never natively.
- MSVC C5246: `std::array` needs double braces, also for a member default, constant or argument (`size{ 1.0F, 1.0F }`, `f({ 255, 255, 255 })`). Arrays of aggregates (`std::array CASES{ Case{ ... }, ... }`) pass.
- MSVC C2641: a `std::array` deduced from scalars (`std::array KEYS{ InputCode::KeyW, ... }`) warns as C5246 does, and double braces stop the deduction: write `std::to_array({ ... })`.
- `std::ranges::find` over a `std::array`: clang-tidy wants `const auto*` (libc++'s iterator is a pointer), which MSVC rejects (its iterator is a class). Search a named `std::span` of the array and keep `const auto`, as `platform/window.cpp` does.
- MSVC C4868: braces around constructor arguments that build a temporary with a destructor (a `std::filesystem::path` copied into a by-value parameter, a `std::move`d one) warn. Use parentheses for such constructor calls.
- MSVC C4623: an aggregate holding a member with no default constructor (`core::State`) warns that its default constructor is implicitly deleted. Give it a constructor (`Loaded` in `game/host.hpp`, `WorldState`).
- MSVC C4686: a function declared returning a `std::array<...>` that nothing has instantiated by then warns at its first call. Name the type (`using Table = std::array<...>`) and `static_assert(sizeof(Table) == ...)` it before the declaration (`gfx/glow_table.hpp`).
