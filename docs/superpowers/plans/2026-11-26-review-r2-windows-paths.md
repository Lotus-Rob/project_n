# R2: Windows paths Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The game starts, logs, loads its font and settings and saves from folders whose names fall outside the Windows ANSI code page.

**Architecture:** One core helper pair turns a path into its UTF-8 text and back; every log or error argument and every path handed to SDL or SDL_ttf goes through it, and `lint` bans `path::string()` in `src/` so none comes back. A UTF-8 `activeCodePage` manifest on every windows-msvc executable makes the ANSI code page UTF-8, which Quill's `std::string` file name needs. Tests run save, settings and font loading under a non-ASCII folder; on windows-msvc they run under Wine, where they fail before the fix.

**Tech Stack:** C++23, std::filesystem, SDL3 / SDL3_ttf, Quill, XMake (msvc-wine), doctest, Python (tools/project_n.py).

**Spec:** `docs/superpowers/specs/2026-11-25-review-mediums-design.md`, area **R2. Windows paths** (full; plan only). Findings: X-CPP-SAFETY-01, X-CPP-SAFETY-02, X-TEXT-L10N-02, X-UNTRUSTED-INPUT-01, X-UNTRUSTED-INPUT-02 in `docs/reviews/2026-11-25-full-codebase-review/` (`x-cpp-safety.md`, `x-text-l10n.md`, `x-untrusted-input.md`).

## History

- No shared helper yet; the conversion is written twice, privately: `PngPath` in `src/gfx/image.cpp:12-15` (path → UTF-8 for SDL) and `FromUtf8` in `src/platform/paths.cpp:11-14` (SDL's UTF-8 → path). The review names the helper `core::PathText`.
- `src/core/log.cpp:50-53` already knows `.string()` throws on Windows outside the ANSI code page, wraps it in try/catch and names the UTF-8 manifest as the remedy.
- `.string()` on a path in `src/`: `main.cpp:89`, `core/saves.cpp:19` (inside `Jobs::Background`: a throw is `std::terminate`), `core/log.cpp:53`, `core/save_file.cpp:33`, `core/logic.cpp:383,388`, `gfx/text_layout.cpp:452` (to `TTF_OpenFont`), `data/yaml.cpp:10,14,24`, `data/settings.cpp:196,210,215`, `app/app.cpp:100,103,269`. In tests: `tests/unit/gfx/text_layout_tests.cpp:81,96`, `tests/unit/gfx/ttf_tests.cpp:36`.
- No manifest in `xmake.lua`. Windows tests run under Wine through `test_under_wine` (`tests/xmake.lua:7`); host Wine is 10.0, the msvc container's comes from its apt snapshot.
- Nothing tried or rejected before (palace, git, docs). The lint model is `platform_maths` / `bypassed_facades` in `tools/project_n.py` with `code_of()`, self-tested in `tools/test_project_n.py:1832`.

## Global Constraints

- Every path that becomes text (log, error, format) or goes to SDL / SDL_ttf goes through `core::PathText`; every UTF-8 `char` path from SDL or the command line comes back through `core::PathOf`.
- `lint` rejects `.string()` and `.generic_string()` on anything in `src/` (no exception list: every hit today is a path).
- Bugs in `src/` are fixed test-first; the RED for this branch is on windows-msvc under Wine (`pixi run project_n test --preset windows-msvc --suite <s>`), since Linux paths are UTF-8 already.
- The non-ASCII folder name in tests is `u8"项目_é_🎮"` (Chinese: outside every Western code page; é: inside 1252, so a code-page round trip that only half works shows; 🎮: outside the BMP). Built as `std::filesystem::path{ u8"..." }`, never from a narrow `std::string`.
- The manifest is a build change: `check --container` from a main checkout, or CI's `container` job on the PR from a worktree.
- Code comments say why, never cite the review, spec or plan.

## Review Focus

1. A save written by the background job to a non-ASCII slot: must not terminate the process (the job's log line). Task 2's saves test.
2. The font opened from a non-ASCII install folder: the game must start. Task 2's text layout test.
3. Settings first-run default write under a non-ASCII user folder (the log that names the path): must not abort start-up. Task 2's settings test.
4. The log file under a non-ASCII folder: must be created with that name, not a mangled one. Task 3's log test.
5. A path whose text is shown back (`PathText` → `PathOf`) must round-trip unchanged, separators included. Task 1's samples.

## Waves

| Wave | Tasks | Needs |
|---|---|---|
| 1 | 1 | main |
| 2 | 2 | Task 1's `core::PathText` / `core::PathOf` |
| 3 | 3 | Task 2 (no `.string()` left in `src/` except `log.cpp:53`, which Task 3 owns) |
| 4 | 4 | Task 3 (its `lint --all` needs `log.cpp:53` gone; it is small, so serial costs little) |

---

### Task 1: `core::PathText` and `core::PathOf`

implementer: sonnet · review: full

**Files:**
- Create: `src/core/path_text.hpp`, `src/core/path_text.cpp`
- Modify: `src/gfx/image.cpp:10-17` (drop `PngPath`), `src/platform/paths.cpp:9-16` (drop `FromUtf8`)
- Test: `tests/unit/core/path_text_tests.cpp`

**Interfaces:**
- Produces: `[[nodiscard]] std::string core::PathText(const std::filesystem::path& path);` (the UTF-8 bytes of `path.u8string()`, native separators) and `[[nodiscard]] std::filesystem::path core::PathOf(std::string_view utf8);` (the path whose `u8string()` is `utf8`). Both `///`-documented: why they exist (SDL and our logs speak UTF-8; `path::string()` speaks the ANSI code page on Windows and throws outside it).

- [ ] **Step 1: Write the failing tests** in `tests/unit/core/path_text_tests.cpp`:
  - `TEST_CASE("a path's text is its UTF-8 bytes")`: ~10 samples as a table `{u8 literal path, expected UTF-8 std::string}`: empty, `a`, `saves/slot.pns`, `é`, `项目`, `🎮`, `项目_é_🎮/settings.yaml`, a name with a space, a dotfile, a trailing separator. Compare `PathText(std::filesystem::path{ sample })` to the bytes, counting misses and checking once (tests/AGENTS.md).
  - `TEST_CASE("a path read from its text is the same path")`: the same ~10 samples: `PathOf(PathText(p)) == p`, checked once.
- [ ] **Step 2: Run** `pixi run project_n test --suite core_tests --test path_text`. Expected: does not compile (no `core/path_text.hpp`).
- [ ] **Step 3: Implement** both functions in `src/core/path_text.{hpp,cpp}`; replace `PngPath(file)` with `core::PathText(file)` in `src/gfx/image.cpp` and `FromUtf8(x)` with `core::PathOf(x)` in `src/platform/paths.cpp` (move their "SDL's paths are UTF-8" reason to the new header's Doxygen).
- [ ] **Step 4: Run** `pixi run project_n test --suite core_tests --test path_text`, then `pixi run project_n test`. Expected: PASS.
- [ ] **Step 5: Commit** (`refactor(core): share one UTF-8 conversion for paths` or as the git-commit skill rules), then `pixi run project_n lint --range <task base>..HEAD`.

### Task 2: no `path::string()` in logs, errors, or SDL calls

implementer: sonnet · review: full

**Files:**
- Modify: every `src/` line in History's list except `src/core/log.cpp:53`; `tests/unit/gfx/text_layout_tests.cpp:81,96`, `tests/unit/gfx/ttf_tests.cpp:36`
- Test: `tests/unit/core/saves_tests.cpp`, `tests/unit/data/settings_tests.cpp`, `tests/unit/gfx/text_layout_tests.cpp`

**Interfaces:**
- Consumes: `core::PathText`, `core::PathOf` (Task 1).
- Produces: `src/` free of `.string()` on paths except `log.cpp:53` (Task 3).

- [ ] **Step 1: Write the failing tests** (the fixtures take their folder name as a `std::filesystem::path` so a `u8` name survives; existing callers pass ASCII and are unchanged):
  - saves_tests: `TEST_CASE("a state is written and read back in a folder named outside the ANSI code page")`: `Folder` named `u8"项目_é_🎮"`, `core::Jobs jobs{ 2 }`, write `State(1'000, 3)`, read, `REQUIRE(read.has_value())`, `CHECK(*read == State(1'000, 3))`, and `CHECK(std::filesystem::exists(folder.Slot()))`.
  - settings_tests: `TEST_CASE("settings are written and read back in a folder named outside the ANSI code page")`: `Scratch` named `u8"项目_é_🎮"`; `LoadSettings(file)` on a missing file writes the defaults (`CHECK(std::filesystem::exists(file))`), then `SaveSettings` of one changed field and `LoadSettings` reads it back.
  - text_layout_tests: `TEST_CASE("the font opens from a folder named outside the ANSI code page")`: copy `Assets() / "fonts" / FONT_FILE` into a temporary `u8"项目_é_🎮"` folder (removed at the end), `gfx::TextLayout::Open(folder)` → `REQUIRE(layout.has_value())`.
- [ ] **Step 2: Run them on windows-msvc** `pixi run project_n test --preset windows-msvc --suite core_tests --test saves`, `--suite data_tests --test settings`, `--suite gfx_tests --test text_layout`. Expected: FAIL or abort (`std::system_error` "No mapping for the Unicode character…", or `std::terminate` from the save job). Record which. On the native preset they pass already: record that too. If any passes on windows-msvc, stop and report (Wine's code page is then already UTF-8, and Task 3's spike changes).
- [ ] **Step 3: Fix** every listed `.string()` with `core::PathText(...)` (logs, `core::Error` texts, `TTF_OpenFont`, `.filename().string()` included); in tests too.
- [ ] **Step 4: Run** the three tests on windows-msvc and native: PASS; then `pixi run project_n test`.
- [ ] **Step 5: Commit** test with fix, per module (`fix(core)`, `fix(data)`, `fix(gfx)`, `fix(app)` as the git-commit skill says), then `pixi run project_n lint --range <task base>..HEAD`.

### Task 3: a UTF-8 code page for windows-msvc executables

implementer: sonnet · review: full

**Spike S1 (first step, measured):** does Wine honour an `activeCodePage` manifest? Measured by: a windows-only doctest case `TEST_CASE("the process's ANSI code page is UTF-8")` → `CHECK(GetACP() == CP_UTF8)` in `tests/unit/core/path_text_tests.cpp` under `#if defined(_WIN32)`, run with `pixi run project_n test --preset windows-msvc --suite core_tests --test path_text` before and after embedding the manifest (host Wine 10.0), and on CI's container job (the msvc image's Wine). Decision rule: passes after and fails before on host Wine → go on; passes on host but fails in the container → stop and report both Wine versions (the controller takes it to the user: bump the image's Wine, or keep the check for native Windows only); fails on host with the manifest provably embedded (`llvm-readobj --coff-resources` lists an RT_MANIFEST holding `activeCodePage`) → stop and report the same way. When: this task's first steps.

**Files:**
- Create: `windows/utf8.manifest` (an `assembly` with `<activeCodePage>UTF-8</activeCodePage>` under `windowsSettings`, namespace `http://schemas.microsoft.com/SMI/2019/WindowsSettings`)
- Modify: `xmake.lua` (one rule, e.g. `utf8_code_page`, that adds the manifest to a binary target on windows; applied to `project_n`), `tests/xmake.lua` (applied in `unit_tests` and to every other binary target there: bench, reference, fuzz if built for windows), `src/core/log.cpp:47-56`
- Test: `tests/unit/core/path_text_tests.cpp` (S1's case), `tests/unit/core/log_tests.cpp` if a log test exists (else the case goes in the core suite file that starts the logger)

**Interfaces:**
- Consumes: `core::PathText` (Task 1).
- Produces: every windows-msvc executable runs with `GetACP() == CP_UTF8`.

- [ ] **Step 1: S1 RED:** add the `GetACP` case; run on windows-msvc. Expected: FAIL (1252 under Wine's default locale).
- [ ] **Step 2: Embed the manifest** with the rule; XMake passes a `.manifest` file in `add_files` to MSVC's linker as `/MANIFESTINPUT` (check the link line with `-v` once). Re-run: PASS. Apply S1's decision rule.
- [ ] **Step 3: Write the failing log test:** `TEST_CASE("the log file is created in a folder named outside the ANSI code page")`: start logging into a temporary `u8"项目_é_🎮"` folder, log one line, stop, `CHECK(std::filesystem::exists(folder / "project_n.log"))`. Run on windows-msvc before Step 4: record the result (with the manifest, `.string()` gives UTF-8 and may already pass; that is fine: the step pins it).
- [ ] **Step 4: log.cpp:** `file = core::PathText(directory / "project_n.log")`; the comment says why that works (Quill takes a narrow file name, read in the ANSI code page, which the manifest makes UTF-8). Keep the try/catch: `create_directories` still throws on a folder that can't be made.
- [ ] **Step 5: Run** `pixi run project_n test` (native) and `pixi run project_n test --preset windows-msvc`: PASS.
- [ ] **Step 6: Commit** (`build(xmake): run windows executables in the UTF-8 code page`, then `fix(core): name the log file in UTF-8`), `pixi run project_n lint --range <task base>..HEAD`. The container gate is CI's `container` job on the PR (this branch is a worktree), or `pixi run project_n check --container` from a main checkout.

### Task 4: `lint` bans `path::string()` in `src/`

implementer: sonnet · review: light

**Files:**
- Modify: `tools/project_n.py` (next to `platform_maths`; called from the lint step at `tools/project_n.py:2196`), `tools/test_project_n.py` (next to `test_platform_maths_in_core_and_game_are_reported`), `src/AGENTS.md` (one rule line: paths become text through `core::PathText`, never `.string()`)

**Interfaces:**
- Produces: `def path_strings(sources) -> list[str]`: `"<file>:<line>: <text>"` for each `src/` `.cpp`/`.hpp` line whose `code_of()` matches `\.(?:generic_)?string\(\)`.

- [ ] **Step 1: Write the failing selftests:** `test_path_strings_in_src_are_reported` (a `src/core/a.cpp` with `p.string()` and `p.filename().generic_string()` → both reported), `test_path_strings_in_comments_strings_and_other_folders_are_not` (`// p.string()`, `"x.string()"`, a `tools/` or `tests/` file → none).
- [ ] **Step 2: Run** `pixi run project_n selftest`. Expected: FAIL (`path_strings` undefined).
- [ ] **Step 3: Implement** `path_strings` and its `Fail` in lint: "paths become text through core::PathText (core/path_text.hpp): path::string() uses the Windows ANSI code page and throws outside it".
- [ ] **Step 4: Run** `pixi run project_n selftest`, then `pixi run project_n lint --all`. Expected: PASS, no `.string()` left (Tasks 2-3 removed them).
- [ ] **Step 5: Commit** `build(tools): reject path::string() in src/` with the AGENTS.md line; `pixi run project_n doctor --docs`.
