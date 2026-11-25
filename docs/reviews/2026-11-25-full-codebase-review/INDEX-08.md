# Finding index 8/11 (standing findings, most severe first)

The ID prefix is the unit file: e.g. CORE-WORLD-03 is in core-world.md (or its -partN files).

| ID | Severity | Verdict | Status | Where | Finding |
|---|---|---|---|---|---|
| X-PERFORMANCE-17 | low | confirmed | new | `src/game/host.cpp:868` | ExploreRoundFoot walks the whole sight diamond every tick even when the foot stays on the same tile |
| X-PERFORMANCE-18 | low | confirmed | new | `src/gfx/seal_draw.cpp:872` | Seals are laid out every frame with no view culling |
| X-PERFORMANCE-19 | low | partially-confirmed | contradicts-recorded-decision | `src/game/travel.cpp:456` | World generation (Travel::Seed) runs both seeded months single-threaded, and a load runs the whole generation again |
| X-PERFORMANCE-20 | low | confirmed | new | `src/game/travel.cpp:243` | ChooseDestination computes every city's weight twice per traveller |
| X-PERFORMANCE-21 | low | confirmed | new | `src/game/npcs.cpp:292` | Relationships::Forget and Travel::Forget are O(E) and O(n) per NPC, so Fold is quadratic in bulk; nothing outside the tests calls it |
| X-PERFORMANCE-22 | low | partially-confirmed | new | `src/core/world_map.cpp:16` | Full-map fog and ground textures take one texel per tile, so MAX_TILES admits maps no GPU texture can hold |
| X-PERFORMANCE-23 | low | partially-confirmed | known-deferred | `src/gfx/internal/features/sprites.cpp:382` | Every new snapshot uploads every instance to the GPU (5.6 MB per tick in the stress scene) |
| X-PERFORMANCE-24 | low | confirmed | known-deferred | `src/core/journey.cpp:501` | Visit builds a reversed route vector only for MakeTileLeg to copy it again; each journey holds four route-length vectors |
| X-PERFORMANCE-25 | low | partially-confirmed | new | `src/core/game_data.hpp:97` | GameData::BaseHash and Hash re-hash the whole map grid on every call |
| X-RESILIENCE-07 | low | partially-confirmed | new | `src/gfx/renderer.cpp:1497` | One missing or corrupt sprite makes the whole scene fatal; no placeholder or degraded path |
| X-RESILIENCE-08 | low | partially-confirmed | new | `src/core/jobs.cpp:206` | Save()/Write() blocks the logic thread when a previous background job (save or replay write) is still running; Load reads synchronously on the logic thread |
| X-RESILIENCE-09 | low | confirmed | new | `src/core/save_file.cpp:79` | Save and replay temp file name is fixed (slot.tmp): leftovers and concurrent writers are unhandled |
| X-RESILIENCE-10 | low | confirmed | new | `src/data/yaml.cpp:19` | Settings written non-atomically and incompletely; any invalid value discards all settings |
| X-RESILIENCE-11 | low | partially-confirmed | new | `src/platform/window.cpp:330` | Window and display handling: occlusion/minimise state can contradict itself, and a swapchain-less frame busy-waits |
| X-RESILIENCE-12 | low | partially-confirmed | new | `src/platform/window.cpp:349` | No handling for a monitor or display change beyond logging; window and settings size are never validated against the display |
| X-RESILIENCE-13 | low | confirmed | new | `src/main.cpp:53` | Start-up opens the window before checking assets and data, and shows a message box only for start-up errors; the dialog omits the log location |
| X-RESILIENCE-14 | low | partially-confirmed | new | `src/core/log.cpp:71` | Logging quality: asynchronous file log with no crash flush, no run-identifying header, and the DEBUG level always on in release |
| X-TEST-STRATEGY-01 | low | deliberate | new | `.github/workflows/ci.yml:252` | Linux ASan/UBSan/TSan, fuzz seed replay and the bench are not in CI or the gate |
| X-TEST-STRATEGY-02 | low | partially-confirmed | new | `tests/AGENTS.md:1` | Reference pictures run on lavapipe on Linux only; no real-GPU or per-platform rendering test |
| X-TEST-STRATEGY-03 | low | partially-confirmed | known-deferred | `tests/reference/reference_tests.cpp:58` | TOLERANCE=2 hides drift; a drift guard is a known gap |
| X-TEST-STRATEGY-04 | low | partially-confirmed | new | `src/app/app.cpp:1` | app.cpp, main.cpp and the SDL input-to-command path have no tests |
| X-TEST-STRATEGY-05 | low | unverifiable | new | `src/gfx/internal/features/sprites.cpp:1` | Source files with no direct test beyond reference or indirect use |
| X-TEST-STRATEGY-06 | low | partially-confirmed | new | `tools/project_n.py:2407` | Coverage is reported but never enforced or tracked |
| X-TEST-STRATEGY-07 | low | partially-confirmed | new | `tests/unit/game/replay_tests.cpp:649` | Long-run and multi-hour determinism, save-growth and soak tests are skipped and not gates |
| X-TEST-STRATEGY-08 | low | partially-confirmed | new | `tests/fuzz/yaml.cpp:1` | Fuzzing is bounded to linux-clang ASan, with no continuous or scheduled run |
| X-TEST-STRATEGY-09 | low | deliberate | new | `tests/bench/headless.cpp:86` | Bench gate compares wall-clock against fixed budgets and is never run automatically |
| X-TEST-STRATEGY-10 | low | unverifiable | new | `tests/support/shipped_world.hpp:1` | Scaling tests stop at the shipped 50,000-NPC map |
| X-TEST-STRATEGY-11 | low | deliberate | new | `tests/AGENTS.md:1` | Test guidance (about 20 samples, no sweeps) leaves exhaustive checks to fuzzing, which does not reach the game rules |
| X-TEST-STRATEGY-12 | low | partially-confirmed | new | `.github/workflows/ci.yml:305` | Windows and macOS native jobs run tests but not the reference pictures or the replay hash across presets |
| X-TEXT-L10N-04 | low | deliberate | known-deferred | `src/gfx/text_layout.cpp:19` | Single CJK-SC font, no fallback chain |
| X-TEXT-L10N-05 | low | partially-confirmed | new | `src/gfx/text_layout.cpp:310` | Missing glyph is silently skipped (glyph_index 0), notdef never drawn |
| X-TEXT-L10N-06 | low | partially-confirmed | known-deferred | `src/core/strings.cpp:14` | String placeholders are not validated and three different fill implementations exist |
| X-TEXT-L10N-07 | low | deliberate | known-deferred | `src/data/strings.cpp:51` | Language set is hard-coded in several places |
| X-TEXT-L10N-08 | low | deliberate | known-deferred | `src/app/hud.cpp:210` | No text overflow, wrapping or truncation policy |
| X-TEXT-L10N-09 | low | partially-confirmed | new | `src/gfx/text_layout.cpp:484` | Prewarm shapes every string many times; Warm uses O(n) contains per glyph occurrence |
| X-TEXT-L10N-10 | low | confirmed | new | `src/gfx/text_layout.cpp:88` | Placeholders recomputed: Prewarm duplicates the HUD and stack composition logic |
| X-TEXT-L10N-11 | low | partially-confirmed | new | `assets/data/strings.yaml:160` | Procedural world has no place, sect or technique name generator; place names only from handwritten table |
| X-TEXT-L10N-12 | low | partially-confirmed | new | `src/core/names.cpp:71` | People names can collide and English names are ambiguous |
| X-TEXT-L10N-13 | low | confirmed | new | `src/app/hud.cpp:205` | Date shown as month/day with no year, era or calendar localisation |
| X-TEXT-L10N-14 | low | unverifiable | new | `src/gfx/text_layout.hpp:43` | SDF at 48 px base may blur dense CJK strokes at small sizes |
| X-UNTRUSTED-INPUT-03 | low | confirmed | new | `src/game/combat.cpp:829` | Combat::Load resizes a volley's target vector to the claimed count before reading any target (4x allocation of the remaining save bytes) |
| X-UNTRUSTED-INPUT-04 | low | unverifiable | new | `src/game/travel.cpp:573` | Travel::LoadDelta rebuilds a full route journey for every ~41-byte plan before checking members exist, are unique, or that the count is plausible |
| X-UNTRUSTED-INPUT-05 | low | partially-confirmed | new | `src/data/manifest.cpp:139` | Manifest page_size/pages/mip relation unvalidated; UploadSprite narrows the transfer size to u32, so a large page truncates the buffer and the copy writes past it |
| X-UNTRUSTED-INPUT-06 | low | unverifiable | new | `tests/fuzz/save.cpp:111` | Save fuzzer never loads back what it saves, pins the load tick, and skips Logic::Restore |
| X-UNTRUSTED-INPUT-07 | low | partially-confirmed | new | `src/core/logic.cpp:331` | Logic::Restore trusts the save's TickCounters; near-u64-max counters wrap and break the 0 = alive sentinel |
| X-UNTRUSTED-INPUT-08 | low | deliberate | new | `src/core/save_file.hpp:25` | Save decompression bomb: a ~32 KiB file legitimately expands to 1 GiB, with file and state held at once |
| X-UNTRUSTED-INPUT-09 | low | confirmed | new | `src/core/replay.cpp:135` | Replay version stamp is only the release version: builds between releases accept each other's replays |
| X-UNTRUSTED-INPUT-10 | low | partially-confirmed | new | `src/core/saves.cpp:15` | Background save job has no exception barrier: bad_alloc or system_error in WriteSaveFile terminates the game |
| X-UNTRUSTED-INPUT-11 | low | confirmed | new | `src/data/settings.cpp:164` | settings.yaml is rewritten in place (truncate then write), and the defaults omit the edge_pan setting |
| X-UNTRUSTED-INPUT-12 | low | confirmed | new | `src/core/save_file.cpp:78` | No single-instance guard: two running games share quicksave.pns.tmp, replay.pnr.tmp and the rotating log |
| X-UNTRUSTED-INPUT-13 | low | partially-confirmed | new | `src/core/replay.cpp:323` | Untrusted strings are logged verbatim, control and escape characters included (console and log injection) |
| X-UNTRUSTED-INPUT-14 | low | confirmed | new | `src/core/log.cpp:54` | A run's log file has no size limit |
| X-UNTRUSTED-INPUT-15 | low | confirmed | new | `src/gfx/internal/resources/sprite_array.cpp:212` | Sprite PNGs are decoded in full before their size is checked against the manifest |
| X-UNTRUSTED-INPUT-16 | low | confirmed | new | `src/core/archive.hpp:107` | Hasher::Put assumes scalars of at most 8 bytes (shift past 64 bits is UB) |
| X-UNTRUSTED-INPUT-17 | low | unverifiable | new | `src/main.cpp:27` | Unknown command-line arguments are silently ignored |
| X-UNTRUSTED-INPUT-18 | low | confirmed | new | `src/app/app.cpp:112` | DataWatch iterates a directory_iterator in a range-for, whose operator++ throws |
| X-UNTRUSTED-INPUT-19 | low | confirmed | new | `src/game/travel.cpp:176` | Saved player route accepted with non-adjacent tiles (teleporting route) |
| X-UNTRUSTED-INPUT-20 | low | deliberate | known-deferred | `src/core/replay.cpp:88` | Replay ReadTicks reserves 16 bytes per remaining byte for tickStarts and held input |
| AGENT-DOCS-10 | info | partially-confirmed | new | `.claude/skills/release/SKILL.md:43` | Skill 'Gotchas' headings are empty in release and look-at-the-game |
| AGENT-DOCS-11 | info | unverifiable | new | `README.md:12` | README and doctor hints use pacman, which conflicts with the user's shelly rule for this machine |
| AGENT-DOCS-12 | info | partially-confirmed | new | `.claude/skills/git-commit/SKILL.md:6` | Skill ownership and the 'owner lists it under Skills' rule is not followed consistently |
| AGENT-DOCS-13 | info | partially-confirmed | new | `AGENTS.md:38` | Docs name tools the rules forbid calling in docs, and run generators outside project_n.py |
| AGENT-DOCS-14 | info | confirmed | new | `.github/AGENTS.md:4` | The count 'nine checks' is hardcoded in two AGENTS.md files |
| AGENT-DOCS-15 | info | deliberate | new | `AGENTS.md:53` | The workflow stacks many review layers for single-developer tasks |
| AGENT-DOCS-16 | info | deliberate | new | `AGENTS.md` | Memory index and AGENTS.md duplicate and can contradict each other |
| APP-INPUT-UI-14 | info | partially-confirmed | new | `src/app/damage_numbers.hpp:128` | Combined damage numbers wait unbounded for a UseEnded that may never come |
| APP-INPUT-UI-15 | info | confirmed | new | `src/app/camera_control.cpp:236` | Camera path pan displacement scales with the current zoom, not the segment's start |
| APP-INPUT-UI-16 | info | confirmed | new | `src/app/camera_control.cpp:667` | Zoom is centred on the screen, not the cursor |
| APP-MAIN-10 | info | deliberate | new | `src/app/app.cpp:536` | Escape quits the game instantly with no confirmation or pause menu |
| APP-MAIN-11 | info | partially-confirmed | new | `src/main.cpp:161` | Logger shutdown relies on process-exit teardown; no explicit flush on early return or fatal paths |
| APP-MAIN-12 | info | partially-confirmed | new | `src/app/app.cpp:792` | Scene resident report can name an intermediate scene when the wanted scene changes mid-load |
| APP-MAIN-13 | info | unverifiable | known-deferred | `src/app/app.cpp:263` | Main thread, waiting on its own fork-join, may run the logic thread's long chunks |
| APP-MAIN-14 | info | confirmed | new | `tests/unit/app/data_watch_tests.cpp:22` | data_watch_tests use fixed names in the shared temp directory |
| ART-TOOLS-10 | info | deliberate | new | `art/world_map.py:1674` | Rivers rewritten on every plain run lose hand-placed sources and edits |
| ART-TOOLS-11 | info | partially-confirmed | new | `art/world_map.py:967` | Quadratic or repeated work in road planning |
| ART-TOOLS-12 | info | confirmed | new | `art/placeholders.py:646` | Mask.disc comparison uses float floor/ceil on products, with platform-independent but order-sensitive results |
| ART-TOOLS-13 | info | confirmed | new | `art/placeholders.py:767` | rune_strokes() regenerated on every rune() call |
| ART-TOOLS-14 | info | partially-confirmed | new | `art/AGENTS.md:4` | AGENTS.md omits placeholders.py usage and the zlib/Python dependency of hashes |
| ART-TOOLS-15 | info | partially-confirmed | new | `art/atlas.sha256:1` | atlas.sha256 hashes every sprite PNG, so any LFS or filter state makes lint fail |
| ASSETS-DATA-08 | info | deliberate | new | `assets/data/abilities.yaml:37` | Ability numbers are placeholders with odd scale relationships |
| ASSETS-DATA-09 | info | deliberate | contradicts-recorded-decision | `assets/data/world.yaml:1` | World map is a single hand-tuned fixed grid whose data comments claim it is also the test map |
| ASSETS-DATA-10 | info | partially-confirmed | new | `assets/data/world.yaml:47` | Danger site on the capital-port trunk road and a danger cost of exactly rock |
| ASSETS-DATA-11 | info | partially-confirmed | new | `assets/data/world_look.yaml:30` | world_look kinds carry a hard-coded 16-kind cap and per-kind sprite sets tied to the 11 terrain kinds |
| ASSETS-DATA-12 | info | confirmed | new | `assets/data/effects.yaml:41` | Presentation defaults and look values duplicated between glows |
| BUILD-ENV-15 | info | confirmed | known-deferred | `xmake-requires.lock:434` | Lock pins different xmake-repo commits per platform |
| BUILD-ENV-16 | info | deliberate | new | `xmake.lua:266` | Tracy is built twice (on and off) in every mode and preset |
| BUILD-ENV-17 | info | confirmed | new | `containers/.containerignore:4` | Image tag hashes pixi.toml and pixi.lock, so any edit rebuilds images |
| BUILD-ENV-18 | info | partially-confirmed | new | `.gitignore:22` | .gitignore patterns are broader than intended |
| BUILD-ENV-19 | info | deliberate | contradicts-recorded-decision | `xmake.lua:153` | -Wno-padded, -Wno-effc++ and the C++98 warnings are disabled globally |
| BUILD-ENV-20 | info | partially-confirmed | new | `xmake.lua:3` | set_xmakever is a minimum, not the exact pin |
| BUILD-ENV-21 | info | confirmed | new | `pixi.toml:44` | Loosely pinned clang compiler package (==22.1) breaks the exact-pin rule |
| BUILD-ENV-22 | info | unverifiable | new | `LICENSE:1` | LICENSE is AGPL-3.0 while binaries link zlib/MIT/Apache/OFL/FTL pieces: compatibility reasoning is not documented |
| BUILD-ENV-23 | info | unverifiable | new | `.vscode/settings.json:10` | VS Code settings require a hand edit and skip-worktree on Windows |
| CI-07 | info | confirmed | new | `.github/workflows/ci.yml:55` | Stale comment about fork PRs; the repo has no pull_request trigger, so fork PRs get no CI |
| CI-08 | info | unverifiable | new | `.github/workflows/ci.yml:433` | No supply-chain attestation, checksums or SBOM for public release archives |
| CI-09 | info | deliberate | new | `.github/dependabot.yml:3` | Dependabot covers only github-actions; pins elsewhere are manual |
| CI-10 | info | partially-confirmed | new | `.github/workflows/ci.yml:253` | Floating runner images and hard-coded Visual Studio path |
| CI-11 | info | confirmed | new | `.github/workflows/ci.yml:161` | Artifacts uploaded for every branch push without a retention limit |
| CI-12 | info | deliberate | new | `.github/workflows/ci.yml:304` | Windows `doctor` step swallows all failures other than exit code 3 only through pwsh semantics; `address` run does not cover commit-check or lint on Windows |
| CI-13 | info | deliberate | new | `.github/rulesets/main.json:14` | Ruleset: no required review, no CODEOWNERS or SECURITY.md, no signed-commit or tag protection |
| CORE-INFRA-21 | info | confirmed | new | `src/core/math.cpp:207` | FP environment check covers only rounding and FTZ/DAZ |
| CORE-INFRA-22 | info | partially-confirmed | new | `src/core/profile.hpp:23` | PN_PROFILE_MESSAGE evaluates its argument twice |
| CORE-INFRA-23 | info | unverifiable | new | `src/core/manifest.hpp:38` | Stale NOLINT on SceneId: performance-enum-size doesn't fire on empty enums |
| CORE-INFRA-24 | info | partially-confirmed | new | `src/core/strings.hpp:150` | Strings::text is a public mutable vector whose invariant the class relies on |
| CORE-INFRA-25 | info | confirmed | new | `src/core/ui_colours.hpp:134` | Small dead code and doc nits in scope |
| CORE-JOURNEY-VOLLEY-12 | info | confirmed | new | `src/core/journey.hpp:95` | Journey::parts documented 'never empty' while the API returns and handles journeys without parts |
| CORE-JOURNEY-VOLLEY-13 | info | unverifiable | new | `src/core/journey.hpp:62` | Stay drift seeds are u32 drawn per journey: same-seed stays drift identically |
| CORE-JOURNEY-VOLLEY-14 | info | partially-confirmed | new | `src/core/journey.cpp:176` | A staying traveller is 'moving' for the whole stay at a drift of ~0.5 units/tick |
| CORE-JOURNEY-VOLLEY-15 | info | confirmed | known-deferred | `src/core/journey.hpp:10` | journey.hpp includes calendar.hpp although only journey.cpp uses it |
| CORE-JOURNEY-VOLLEY-16 | info | confirmed | new | `src/core/volley.cpp:13` | Doc comments on the volley's random purposes are grouped so Doxygen attaches them to the wrong constant |
