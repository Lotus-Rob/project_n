# Finding index 11/11 (standing findings, most severe first)

The ID prefix is the unit file: e.g. CORE-WORLD-03 is in core-world.md (or its -partN files).

| ID | Severity | Verdict | Status | Where | Finding |
|---|---|---|---|---|---|
| TOOLS-A-19 | info | deliberate | new | `tools/project_n.py:495` | prune_tidy deletes every cached result when lint never reached clang-tidy |
| TOOLS-A-20 | info | partially-confirmed | new | `tools/AGENTS.md:22` | AGENTS.md entries are essays, not one line per rule, and duplicate code docstrings |
| TOOLS-A-21 | info | partially-confirmed | new | `tools/AGENTS.md:9` | AGENTS.md names three single sources of truth, but more tables in the file also act as one |
| TOOLS-A-22 | info | unverifiable | new | `tools/project_n.py:944` | Parallel local runs race on shared directories |
| TOOLS-A-23 | info | confirmed | new | `tools/project_n.py:943` | Hard-coded lavapipe ICD name and container UID |
| TOOLS-A-24 | info | confirmed | new | `tools/project_n.py:1293` | Include lints are bypassed by relative includes and angle-bracket spellings |
| TOOLS-A-25 | info | confirmed | new | `tools/project_n.py:1212` | --range with a three-dot or single-ref value silently widens to a full run |
| TOOLS-A-26 | info | confirmed | new | `tools/project_n.py:1155` | python_comments can crash lint on a file that fails to tokenize or parse |
| TOOLS-B-18 | info | confirmed | new | `tools/project_n.py:129` | INSTALL_HINTS holds dead entries for pixi and git |
| TOOLS-B-19 | info | unverifiable | new | `tools/project_n.py:1428` | preprocess_command keeps every compile flag except -c/-o and runs the real compiler with -E |
| TOOLS-B-20 | info | confirmed | new | `tools/project_n.py:1506` | Cached clang-tidy output embeds absolute paths and is shared by key across checkouts |
| TOOLS-B-21 | info | partially-confirmed | contradicts-recorded-decision | `tools/project_n.py:1314` | Determinism lint covers only libm calls, with a few missing names |
| TOOLS-B-22 | info | deliberate | new | `tools/project_n.py:550` | Any pixi.toml edit, even a comment, invalidates the package cache |
| TOOLS-B-23 | info | confirmed | new | `tools/project_n.py:2257` | No-upstream main silently skips commit-check in `check` |
| TOOLS-B-24 | info | partially-confirmed | new | `tools/project_n.py:1732` | DXIL from Linux dxc may be unsigned; Linux- and Windows-built binaries may differ |
| TOOLS-B-25 | info | unverifiable | new | `tools/test_project_n.py:2702` | Self-tests include slow art-generator tests and read live repo constants |
| TOOLS-B-26 | info | unverifiable | new | `tools/test_project_n.py:1` | Functions with no test coverage |
| TOOLS-B-27 | info | confirmed | new | `tools/project_n.py:3169` | prune_worktrees ignores ignored files and treats an ancestor branch as 'no commits yet' |
| TOOLS-B-28 | info | partially-confirmed | new | `tools/project_n.py:2089` | check_staged_format runs clang-format serially, one subprocess pair per file |
| TOOLS-B-29 | info | confirmed | new | `tools/project_n.py:2402` | artifact zips are not reproducible and the Windows/macOS dependency check is partial |
| TOOLS-B-30 | info | unverifiable | new | `tools/cliff.toml:27` | cliff.toml puts every conventional type into the player-facing changelog |
| TOOLS-B-31 | info | confirmed | new | `tools/project_n.py:2045` | prune-ci-caches looks at only the newest 1000 entries |
| X-ACCESSIBILITY-17 | info | partially-confirmed | new | `docs/design/0002-engine-architecture.md:160` | Camera shake, hit-stop and other motion planned in design 0002 have no reduce-motion hook |
| X-ACCESSIBILITY-18 | info | deliberate | contradicts-recorded-decision | `docs/superpowers/specs/2026-11-10-phase6-chunk11-settings-design.md:107` | Accessibility rule 'settings never change difficulty' excludes game-speed and assist options |
| X-ARCHITECTURE-21 | info | partially-confirmed | new | `src/app/app.cpp:125` | Command-line parsing is duplicated across main.cpp, app.cpp and core/command_line |
| X-ARCHITECTURE-22 | info | unverifiable | new | `src/core/game_data.hpp:69` | The world map is an immutable object shared by both threads, so the world can't change at run time |
| X-ARCHITECTURE-23 | info | partially-confirmed | new | `src/app/app.hpp:34` | app.hpp and game.hpp include game/host.hpp, so app compiles against the whole game module's internals |
| X-CONCURRENCY-17 | info | partially-confirmed | new | `src/app/app.cpp:263` | App constructor comment understates when the main thread runs simulation chunks |
| X-CONCURRENCY-18 | info | unverifiable | new | `src/core/thread_safety.hpp:1` | Thread-role invariants (logic-only, main-only, same thread each time) are comments only; annotations could enforce them |
| X-CONCURRENCY-19 | info | confirmed | new | `src/game/host.hpp:452` | Host comment says journey chunks 'share nothing else', but they share the walk-table cache and kept walks |
| X-CONCURRENCY-20 | info | confirmed | new | `src/platform/sdl_log.cpp:14` | noexcept SDL log callback can throw from logging |
| X-CPP-SAFETY-10 | info | deliberate | contradicts-recorded-decision | `src/AGENTS.md:39` | Enum dispatch is never checked for completeness: the switch-warning clash forces if-chains everywhere |
| X-CPP-SAFETY-11 | info | confirmed | new | `src/core/archive.hpp:109` | The archive Hasher would hit UB for scalars wider than 8 bytes; nothing stops one being archived |
| X-CPP-SAFETY-12 | info | confirmed | new | `src/core/replay.cpp:160` | Replay header narrows the game version's length to u8 without a check |
| X-CPP-SAFETY-13 | info | partially-confirmed | new | `src/core/handover.hpp:159` | Handover indexes its fixed arrays unchecked in release |
| X-CPP-SAFETY-14 | info | partially-confirmed | new | `src/core/manifest.hpp:38` | NOLINT and pragma audit: every suppression in src/ is narrow and justified |
| X-DETERMINISM-10 | info | deliberate | contradicts-recorded-decision | `docs/design/0002-engine-architecture.md:267` | The claim that contraction can't make a binary disagree with itself is stronger than LTO, inlining and constant folding guarantee |
| X-DETERMINISM-11 | info | confirmed | new | `src/game/host.cpp:1468` | Fog and sight helpers write `m_world` while saving and hashing use `Live()`; `Explore()` and `SetSightRadius()` are test-only production API |
| X-DETERMINISM-12 | info | confirmed | new | `tests/support/exactness.hpp:55` | `UnlikeOnBothWorkerCounts` never compares one worker count against another |
| X-DETERMINISM-13 | info | unverifiable | new | `src/game/random_purposes.hpp:111` | RNG purposes and seeds are coordinated only by comments across modules; the gfx terrain seed is tied to the constant game seed |
| X-DETERMINISM-14 | info | confirmed | new | `src/game/npcs.cpp:354` | A stop request mid-calendar-step leaves a half-applied month |
| X-PERFORMANCE-26 | info | unverifiable | new | `src/core/draw_list.cpp:115` | DrawList::Publish copies every instance three times (parts, then a concatenation, then the sorted snapshot) before the GPU copy |
| X-PERFORMANCE-27 | info | unverifiable | new | `src/app/view.cpp:65` | Hover picking scans every snapshot instance each frame, unpacking halves |
| X-PERFORMANCE-28 | info | confirmed | new | `src/gfx/renderer.cpp:1331` | Each frame repeats work that is fixed within a frame: Holds() scans the scene's sprites about 7 times, and GatherLabels resets a table the size of the strings table |
| X-PERFORMANCE-29 | info | deliberate | new | `src/gfx/cloud_draw.cpp:429` | Over MAX_CLOUD_INSTANCES, the clouds dropped are those at the top of the view |
| X-TEST-STRATEGY-13 | info | deliberate | contradicts-recorded-decision | `tests/unit/game/replay_tests.cpp:164` | No golden hash: determinism regressions that hold within one binary are invisible across commits |
| X-TEST-STRATEGY-14 | info | unverifiable | new | `tests/unit/game/replay_tests.cpp:164` | Hash-collision and comparison risk: replay tests compare only 64-bit hashes of states |
| X-TEST-STRATEGY-15 | info | partially-confirmed | new | `tests/AGENTS.md:1` | Test design doc and guidance describe stale gate numbers and heavy prose |
| X-TEXT-L10N-15 | info | partially-confirmed | known-deferred | `src/core/strings.cpp:24` | No locale or number formatting beyond plain decimal |
| X-TEXT-L10N-16 | info | deliberate | known-deferred | `src/gfx/text_layout.cpp:267` | Vertical text and BTT workaround depends on a SDL3_ttf 3.2.2 bug |
| X-TEXT-L10N-17 | info | confirmed | new | `src/core/strings.hpp:158` | Strings::Find scans the named list linearly before hashing |
| X-TEXT-L10N-18 | info | partially-confirmed | new | `assets/fonts/PROVENANCE.md:8` | Licence handling for the font is correct but undocumented checks |
| X-UNTRUSTED-INPUT-21 | info | confirmed | new | `src/data/manifest.cpp:64` | Manifest sprite file paths are not confined to assets/ |
| X-UNTRUSTED-INPUT-22 | info | confirmed | new | `src/core/save_file.cpp:13` | Save and replay containers share the PN_SAVE magic; the file kind is not recorded |
| X-UNTRUSTED-INPUT-23 | info | confirmed | new | `src/core/saves.hpp:31` | Saves documents 'the thread that created the job system', but the logic thread calls it |
| X-UNTRUSTED-INPUT-24 | info | confirmed | new | `tools/project_n.py:780` | Fuzzing runs only on request; no scheduled or CI fuzzing time |
| X-UNTRUSTED-INPUT-25 | info | unverifiable | new | `src/main.cpp:52` | PN_SOURCE_DATA is a narrow absolute path decoded in the ANSI code page on Windows debug builds |
