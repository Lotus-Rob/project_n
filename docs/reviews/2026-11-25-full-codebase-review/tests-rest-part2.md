# Review: tests-rest (part 2)

### TESTS-REST-11: Shipped-content pinning tests couple tests to a map that is only a test map

- **Severity:** low · **Verdict:** unverifiable · **Category:** test-quality · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `tests/unit/data/world_map_tests.cpp:652`

world_map_tests pins specifics of assets/data/world.yaml: 'about six cities and twenty danger sites', on_foot at pace 16 as profile 0, wary-walker rules and every road end on another road's curve. strings_tests asserts literal translations such as Seal gold -> 法印金. The decision record says world.yaml is only a test map because maps are procedural per seed.

**Impact:** Every edit of the test map or of a translation needs a test edit, and they test the data file, not the reader. As generated worlds arrive these tests become dead weight or invite the shipped map to stay frozen.

**Recommendation:** Move the reader-behaviour parts to small inline maps (many already exist). Keep one shipped-data validity test, and an invariant check (no empty strings, both languages) without literal text.

**Verification:** I did not open world_map_tests.cpp:652 or check the world.yaml-is-a-test-map decision record, so I can't judge whether the pinning contradicts it. The strings_tests literal translations (Fox -> 狐 etc.) exist as claimed. Pinning a shipped table's content in a data test is common and mostly harmless.

### TESTS-REST-12: Oversized test files and untested units in gfx and app

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** coverage-gap · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `tests/unit/gfx/labels_tests.cpp:1`

labels_tests.cpp is 1157 lines with 72 cases, and many of its constants are named after their values (AT_100, AT_110, ..., although tests/.clang-tidy disables the magic-number checks). reference_tests.cpp is 2254 lines. src/gfx/colour.cpp has no test, src/data/yaml.cpp (254 lines, shared by every reader, including the known alias warning amplification) has no direct test file, and src/app/app.cpp (907 lines: the main loop, input glue and DataWatch) has only tests for its extracted helpers.

**Impact:** Large files slow review and compile. The shared YAML helpers are tested only through consumers, so the known NoteUnknownKeys amplification has no regression test. Value-named constants add noise for nothing.

**Recommendation:** Split labels_tests by feature (stacking, fade, keep-clear). Add a yaml_tests.cpp (unknown-key notes, alias expansion bounds, line numbers). Drop AT_* naming in favour of literals since the tidy check is off for tests.

**Verification:** Sizes are right (labels_tests 1157 lines, reference_tests 2254, yaml.cpp 254, app.cpp 907). colour.cpp has no test file. AT_100/AT_110/AT_120 constants exist at labels_tests.cpp:35-37. No test references NoteUnknownKeys, and the alias amplification is recorded in sweep-up-backlog.md line 34 as a known deferral. colour.hpp is used by many tests indirectly. Splitting files and renaming constants are style preferences.

### TESTS-REST-13: DataWatch tests miss a file added, and the watch is blind to same-timestamp edits

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** coverage-gap · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/unit/app/data_watch_tests.cpp:50`

DataWatch compares sorted (filename, last_write_time) listings (src/app/app.cpp:95-123). Tests cover delete, older replacement and unchanged. They do not cover an added file, a folder that becomes unreadable (which makes the listing empty and so reports a change), or an edit within the filesystem's timestamp resolution (1-2 s on some filesystems, with size not compared).

**Impact:** A hot reload can miss an edit made immediately after the previous one on a coarse-timestamp filesystem. An unreadable folder reports a 'change' that triggers a reload of data that cannot be read. Neither behaviour is documented by a test.

**Recommendation:** Add tests for add and unreadable-folder, and include file size in the listing (or document the limit).

**Verification:** DataWatch (app.cpp:116) compares only filename and last_write_time, so a same-timestamp edit goes unseen. Its tests cover delete, older replacement and unchanged only, with no add or unreadable case. The claim that an unreadable folder 'reports a change' is only partly accurate: the code logs it once with m_unreadable and the empty listing then differs from the seen one. That is the intended behaviour, and the constructor comment says the first Changed() logs it. Timestamp resolution is a minor real limit on modern filesystems, and a missing add test is a small gap.

### TESTS-REST-14: Cast tests encode keyboard and gamepad casts needing a mouse cursor

- **Severity:** info (reviewer said medium) · **Verdict:** deliberate · **Category:** accessibility · **Status:** known-deferred · **Reviewer confidence:** medium
- **Where:** `tests/unit/app/cast_tests.cpp:123`

'a key cast without a cursor sends nothing' and 'a press in combat casts its slot at the point' assert that key presses cast only at a cursor point. Together with the camera_control tests, they lock in the known issue that gamepad and keyboard casts aim at the mouse cursor.

**Impact:** Fixing audit item 4 (casts for gamepad and keyboard-only play, the project's top priority) will break these tests by design. There is no test describing the intended behaviour: aim from a target cursor, facing or auto-target.

**Recommendation:** Add the intended-behaviour test (skipped or tagged) when the aim design is decided, and note the existing cases as provisional.

**Verification:** sweep-up-backlog.md Audit item 4 records exactly this: gamepad and keyboard casts aim at Window::Cursor(), and it needs a small design before any fix. The tests encode current behaviour, and the fix will change them. Adding a skipped future-behaviour test before the design exists would be speculative. Treat it as a known deferral.

### TESTS-REST-15: ArenaSave hand-mirrors the Host save layout through 14 positional parameters

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** maintainability · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/support/saves.hpp:190`

ArenaSave writes the Host save format byte for byte, in order, with a 14-parameter signature (many defaulted, two optionals, a nullable data pointer). WorldSaveAt calls it with 14 positional arguments, most of them std::nullopt or {}.

**Impact:** Every save layout change needs a matching edit here. A swapped or misplaced argument of the same type compiles. The independence of this writer is also worth less when it copies the production order.

**Recommendation:** Take a struct of designated-initialised fields (SaveSpec) instead of positional arguments. Add a layout-version assertion shared with the production writer.

**Verification:** ArenaSave in tests/support/saves.hpp has a long positional signature, and WorldSaveAt (line 277) passes std::nullopt and {} positionally. This is maintainability friction only. The writer is deliberately an independent mirror, so the layout-copy complaint is weak.

### TESTS-REST-16: Tautological and third-party tests that pin constants rather than behaviour

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** test-quality · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/gfx/meshes_tests.cpp:15`

'the mesh buffer holds 4 MiB of vertices' asserts FULL * MESH_VERTEX_BYTES == 4 MiB, which restates two constants. present_mode_tests re-states the PresentModeOrder tables literally. ttf_tests 'SDL3_ttf links HarfBuzz' tests a dependency's build flags (and leaks TTF_Init if a REQUIRE fails).

**Impact:** These change-detector tests fail on a deliberate constant change and never on a bug, which trains people to edit them mechanically. The ttf case is a useful packaging guard but sits among behaviour tests.

**Recommendation:** Drop the 4 MiB and order-restating cases, or make them assert a behaviour (overflow counted, fallback chosen when a mode is unsupported). Keep the HarfBuzz check but guard TTF_Quit with a scope guard.

**Verification:** meshes_tests.cpp:19 'holds 4 MiB' is a constant-restating test, as claimed. The ttf_tests TTF_Init/REQUIRE/TTF_Quit leak on a REQUIRE failure is real (lines 22-28, 32-44) but trivial, and it matters only when the test is already failing. The present_mode claim was not checked.

### TESTS-REST-17: Duplicate and boilerplate-heavy data tests

- **Severity:** info · **Verdict:** confirmed · **Category:** duplication · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/data/strings_tests.cpp:152`

The fox-name and stack-label assertions at lines 152-159 are repeated verbatim in 161-174. The shipped strings.yaml read (assets dir, ifstream, istreambuf_iterator) is copy-pasted three times (162, 177, 203). A misleading stale comment at data/seals_tests.cpp:14 ('One seal with one ring, the least a table holds: the table a reload starts from') sits above ErrorFor, which does something else. Two test names ('a data string given twice' and 'a string given twice') differ only in which table.

**Impact:** Noise and drift. A reader trusts the wrong comment.

**Recommendation:** Add a ShippedText(name) helper in support, delete the duplicated assertions, and fix or delete the stale comment.

**Verification:** strings_tests.cpp:152-159 and the shipped-table test at 161-174 repeat the same fox-name and stack-label assertions. The ifstream/istreambuf read is copy-pasted (162, 177, 203). The seals_tests.cpp:14 comment 'One seal with one ring...' sits above ErrorFor, which only parses and returns the error, so the comment is stale. Pure noise.

### TESTS-REST-18: Parser tests assert exact full error text, bundled in table cases

- **Severity:** info · **Verdict:** deliberate · **Category:** test-quality · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/unit/data/seals_tests.cpp:22`

Many data tests loop over a table of Broken{why,text,says} and CHECK the full message string, with several refusals per TEST_CASE (seals_tests.cpp:22, 52, 103, 138, 315; world_look_tests; world_map_tests). Each case is CAPTUREd, so the failing sample is named.

**Impact:** One reworded message fails a case with many samples, and the first failure in a loop does not stop the rest. Per-behaviour granularity (AGENTS: 'tests one behaviour') is lost, and the harness's failure is harder to attribute.

**Recommendation:** Acceptable as is. Consider asserting a stable prefix (the line number and the key name) rather than the whole sentence, and use SUBCASE per entry.

**Verification:** The finding itself says 'Acceptable as is'. Exact error text with the line number is a deliberate contract for parser messages, and CAPTURE names the failing sample. I only skimmed the seals_tests cases, and tests/AGENTS.md:9 allows table-driven samples.

### TESTS-REST-19: log_startup and platform tests leave files in the working directory

- **Severity:** info · **Verdict:** confirmed · **Category:** test-isolation · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/unit/log_startup/fallback_tests.cpp:10`

fallback_tests writes `project_n_log_fallback` in cwd and never removes it. sdl_log_tests uses `project_n_sdl_log_tests` in cwd, removing it only at the start of the next run.

**Impact:** Files left in the build directory. Harmless in a build tree, but a sibling process under the same cwd (Wine or parallel invocation of the same binary) can see stale logs.

**Recommendation:** Remove the file at the end, or use a unique scratch folder.

**Verification:** fallback_tests.cpp:10-13 creates project_n_log_fallback in cwd and never removes it. sdl_log_tests removes its directory with remove_all only at the start (line 15). The impact is trivial.

### TESTS-REST-20: Fuzz save target starts every input at the largest representable tick

- **Severity:** info · **Verdict:** confirmed · **Category:** fuzz-quality · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/fuzz/save.cpp:20`

SAVED_AT = u64 max - 1 and the next tick is u64 max, so every loaded input is ticked at the extreme. This is deliberate so a flash dated at any tick loads, but it means the post-load Tick, Publish and Save run only at the wrap boundary.

**Impact:** Paths that behave differently at realistic ticks (calendar and journey arithmetic, fog easing) are exercised by the fuzzer only at the extreme. Overflow bugs show up, but realistic-tick state bugs may not.

**Recommendation:** Alternate between a small tick and the extreme by input hash, or run two targets.

**Verification:** SAVED_AT is u64 max - 1 (save.cpp:20), and both Load calls at lines 51 and 78 use it, with NEXT = SAVED_AT + 1. Every input is therefore ticked at the extreme. The reviewer concedes this is deliberate. Realistic-tick coverage comes from the unit tests, so the impact is low.
