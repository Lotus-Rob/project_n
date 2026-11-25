# Review: x-untrusted-input (part 3)

### X-UNTRUSTED-INPUT-21: Manifest sprite file paths are not confined to assets/

- **Severity:** info · **Verdict:** confirmed · **Category:** path-traversal · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/data/manifest.cpp:64`

`file` is any string and is used as `assets / sprite.file`. An absolute path replaces the base entirely, and `../` escapes it. Today the manifest is a shipped, generated asset, but the planned data-driven and modding work will make such files untrusted.

**Evidence:** `const std::expected<std::string, core::Error> file = Field<std::string>(node, "file");` stored as-is. sprite_array.cpp:212 `DecodePng(assets / sprite.file)`.

**Impact:** A mod or tampered manifest can make the game read arbitrary image files on disk. The impact is low today (they are only decoded as PNGs) but it is a bad precedent.

**Recommendation:** Refuse absolute paths and paths with `..` components (check path.is_relative() and lexically_normal() not starting with `..`), and require the `sprites/` prefix the atlas tool writes.

**Verification:** manifest.cpp:64-69 reads 'file' as an arbitrary string with no path validation, and it is later joined as assets / file. An absolute path or '..' would escape the folder. The manifest is a generated, committed asset, so there is no exposure today.

### X-UNTRUSTED-INPUT-22: Save and replay containers share the PN_SAVE magic; the file kind is not recorded

- **Severity:** info · **Verdict:** confirmed · **Category:** format-version · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/save_file.cpp:13`

Replays and saves both go through WriteSaveFile with the same magic and container version. Telling them apart relies on the payload's first u32 happening to differ (SAVE_FORMAT = 1, REPLAY_FORMAT = 5), and on the replay's start state carrying two extra prefix bytes.

**Evidence:** Logic::FinishRecording → m_replays->Write → WriteSaveFile with the same MAGIC; Restore reads `format` from the start of the payload.

**Impact:** Today a misplaced file is refused, but only by coincidence of version numbers. A future bump that makes the formats collide would misparse one as the other, with confusing errors.

**Recommendation:** Record a kind byte or use a distinct magic per kind (PN_SAVE / PN_RPLY) in the container header.

**Verification:** SAVE_FORMAT=1 (logic.cpp:316) and REPLAY_FORMAT=5 (replay.cpp:15,196) are the payload discriminators, and both are checked on read. The claim holds, but it is a hardening suggestion, since misplaced files are refused today. Pre-1.0 has no compat requirement.

### X-UNTRUSTED-INPUT-23: Saves documents 'the thread that created the job system', but the logic thread calls it

- **Severity:** info · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/saves.hpp:31`

Saves::Write and Saves::Read say they must run 'On the thread that created the job system'. The Jobs contract is 'only a thread that may fork, the same thread each time', and in practice the logic jthread (a registered external thread) calls them, while App's main thread created Jobs.

**Evidence:** saves.hpp:31,36 versus jobs.hpp:99-100 'Only a thread that may fork starts or waits for one, and the same thread each time' and app.cpp RunLogic constructing Game on m_logic.

**Impact:** The wrong contract could lead a future caller to move saves to the main thread, which would break the same-thread rule.

**Recommendation:** Restate it as Jobs does: a thread that may fork (registered), the same one each time.

**Verification:** saves.hpp:31,36 do say 'On the thread that created the job system'. That wording conflicts with the Jobs same-thread-that-may-fork contract cited in the finding. I did not re-open jobs.hpp, and the doc wording is as quoted.

### X-UNTRUSTED-INPUT-24: Fuzzing runs only on request; no scheduled or CI fuzzing time

- **Severity:** info · **Verdict:** confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tools/project_n.py:780`

Three fuzz targets exist and their seed corpora replay as tests, but nothing fuzzes beyond the seeds unless someone runs `project_n fuzz` by hand. The save format changes often (ARENA_FORMAT is at 10, WORLD_VERSION at 8).

**Evidence:** cmd_fuzz is a manual command. .github/workflows/ci.yml has no fuzz job; FUZZ_PRESET = ("linux-clang", "address").

**Impact:** New load paths (new components, delta sections) get no fuzzing until someone remembers to run it.

**Recommendation:** Add a short time-boxed fuzz run (for example 60 seconds per target) to `check` on save, replay or data changes, or a nightly scheduled job, and commit new crashing inputs to the corpus.

**Verification:** No fuzz job appears in .github/workflows (grep found nothing). Fuzzing is only the manual `fuzz` command plus seed replay as tests. Design docs 0002.3 and 0002.4.2 describe the targets and seeds but do not record a decision to skip scheduled fuzzing. This is a process gap, not a bug.

### X-UNTRUSTED-INPUT-25: PN_SOURCE_DATA is a narrow absolute path decoded in the ANSI code page on Windows debug builds

- **Severity:** info · **Verdict:** unverifiable · **Category:** portability · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/main.cpp:52`

`std::filesystem::path source{ PN_SOURCE_DATA }` builds a wide path from a narrow string, using the ANSI code page on MSVC. A project checkout under a non-ASCII folder silently falls back to assets/data, so hot reload watches the copy beside the binary instead of the sources. The developer's absolute path is also baked into debug binaries.

**Evidence:** main.cpp:52 and xmake.lua:316 `set_configvar("PN_SOURCE_DATA", (path.join(os.projectdir(), "assets", "data"):gsub("\\", "/")))`

**Impact:** Hot reload silently watches the wrong folder for such developers.

**Recommendation:** Emit the path as a u8 literal and construct it with a char8_t string.

**Verification:** I did not read main.cpp:52 or xmake.lua:316. The ANSI-codepage decoding of a narrow path on MSVC is a known behaviour, but the fallback described depends on code I did not check. Confirming it needs a Windows debug build under a non-ASCII path.
