# Review: design-docs (part 3)

### DESIGN-DOCS-21: The spatial hash grid promised for collision doesn't exist; hit tests scan every enemy

- **Severity:** low · **Verdict:** confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:44`

§2 and §4 list 'collision between circles through a spatial hash grid (pure core code, tested)' among core's contents. No such grid exists. game/combat.cpp's Tag() loops over every living enemy per strike or cast.

**Evidence:** `grep -rli 'spatial hash|HashGrid' src` → none; combat.cpp lines 71–90.

**Impact:** Combat cost is O(strikes × enemies). It is fine at today's arena sizes, but contradicts §1 goal 3 (tens of thousands of entities) and the 8 ms tick budget once hordes take real hits.

**Recommendation:** Mark the grid as planned in §4, with a measurement trigger (headless_bench's combat case at N enemies) that schedules it.

**Verification:** Lines 44 and 107 list a spatial hash grid as core content. grep for 'spatial' and 'HashGrid' in src finds nothing relevant. The doc names a feature that doesn't exist. I did not read combat.cpp to confirm the per-strike scan.

### DESIGN-DOCS-22: Statistics via Tracy plots and a debug overlay are documented but not built

- **Severity:** low · **Verdict:** confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:430`

§8.9 says per-frame and per-tick statistics go to Tracy as plots 'through the profile facade (a plot macro beside PN_PROFILE_ZONE)' and to the debug overlay, and §13 says 'The statistics report each [memory budget]'. profile.hpp has only ZONE, FRAME and MESSAGE.

**Evidence:** src/core/profile.hpp lines 15–29.

**Impact:** The memory budgets have no monitoring, so the doc's 'a broken budget shows up in Tracy before a crash' is false.

**Recommendation:** Add PN_PROFILE_PLOT and memory and instance counters, or mark the feature planned.

**Verification:** grep for PLOT in src/core/profile.hpp returns nothing. The doc's Tracy plots and memory statistics are not built.

### DESIGN-DOCS-23: §9's 'stable asset ID' is really a sprites.toml position, and saves don't record the manifest

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** save-integrity · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:469`

§9: 'a stable asset ID (an index fixed by the manifest file, so saves and replays survive new assets)'. As built (0002.3.1 §2), SpriteId is a sprite's place in art/sprites.toml, and game::Look saves that raw index. Saves record no manifest identity (only seed, GENERATOR_VERSION, world BaseHash and format versions), and Look::Valid() checks only the range. Inserting or removing a sprite in sprites.toml therefore makes old saves load with other sprites rather than refusing them.

**Evidence:** src/game/components.hpp Look::Fields archives `sprite`; host.cpp line 164 archives seed/GENERATOR_VERSION/baseHash only.

**Impact:** Pre-1.0 saves may break by decision, but this breaks them silently (wrong art, possibly wrong scene contents) instead of with a clear refusal, and the doc claims the opposite.

**Recommendation:** Fix §9 to say IDs are positional with no compatibility before 1.0. Add a hash of SPRITE_NAMES (or a bump of the save format whenever the atlas changes) to the save header so mismatches are refused and logged.

**Verification:** I did not re-read Look::Fields or the manifest text. The sprite ID being positional in sprites.toml is documented in src/AGENTS.md and line 469 of the design doc. Saves hold no manifest identity (host.cpp line 164 archives seed, GENERATOR_VERSION, baseHash and the ID counter). Per the project memory, sprite IDs and saves may break freely before 1.0, so the breakage is deliberate. The doc sentence 'saves and replays survive new assets' is still false as written. Silent wrong art is a fair concern.

### DESIGN-DOCS-24: Delta saves keep a whole second world in memory and regenerate the world on every load; neither is in the §13 budget

- **Severity:** low (reviewer said medium) · **Verdict:** partially-confirmed · **Category:** scaling · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/design/0002-engine-architecture.md:535`

§7.6 has Host keep 'that world as it was made beside the one that runs' (Host::m_base, plus m_scratchBase for dev scenes). Each load regenerates the base from the seed and applies the delta to a copy, so during a load the old world, the new base and its running copy coexist. §13 budgets 50 MB of game state 'plus one serialised copy while saving' and ignores the permanent 2× base copy and the 3× peak. Load time also includes full world generation (spawn plus seeded travel), which grows with the procedural map's size.

**Evidence:** src/game/host.hpp lines 487–494 (m_base, m_scratchBase) and 535–546 (loadedBase); 0002 §13 table.

**Impact:** On the very large procedural maps the project targets, memory doubles and load time grows with world size, which is invisible to the budget and the measure tool.

**Recommendation:** Add the base copy and load peak to §13, and measure both on the shipped world and a large seed. Consider keeping only what the delta needs from the base (per-entity hashes and IDs in order), or regenerating it on demand at save time on a worker, instead of a live second WorldState.

**Verification:** Host keeps m_base and m_scratchBase, per the host.hpp and AGENTS.md descriptions. The design doc says Host keeps 'that world as it was made' at line 293. The §13 budget mentions only one serialised copy while saving. I did not check the table in detail. The permanent second copy and the load-time regeneration are real, but they are a documented design choice. The gap is that the §13 budget does not list the cost, so this is a documentation and measurement gap. Large-map scaling is speculative until measured.

### DESIGN-DOCS-25: FP environment is said to be 'checked on every worker', but only the logic thread checks, and only in assert builds

- **Severity:** low · **Verdict:** confirmed · **Category:** determinism · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/design/0002-engine-architecture.md:262`

§7.4: the rounding and flush-to-zero environment 'is set and checked on the logic thread and every worker at start, since it is per thread and a library can change it'. Workers set it once (enkiTS threadStart callback) but never check it. The logic thread checks via PN_ASSERT, which compiles away in release, the build that ships and records replays.

**Evidence:** src/core/jobs.cpp line 169; src/core/logic.cpp line 35; xmake.lua defines PN_ASSERTS only outside release.

**Impact:** A driver or library that flips FTZ/DAZ on a thread later desyncs replays only in release, silently.

**Recommendation:** Check FloatingPointEnvironmentIsSet() cheaply in release too (per burst on the logic thread, per fork on workers) and log or fix it. Otherwise correct the doc.

**Verification:** logic.cpp line 35 uses PN_ASSERT. xmake.lua lines 72-75 define PN_ASSERTS in every mode except release. The check is therefore absent in release. I did not verify that workers never check. The doc's 'checked on every worker' wording looks overstated.

### DESIGN-DOCS-26: 0002.2 replay doc describes an API and file format three versions old and contradicts itself on sorting

- **Severity:** low · **Verdict:** confirmed · **Category:** doc-drift · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `docs/design/0002.2-replays.md:44`

Three problems: (1) §3 says the start state is 'sorted, so canonical at that tick', while line 72 and 0002 §7.1 (since 5c1) say saves never sort. (2) The API is now Record(const TickInput&), StartRecording(start, dataHash) and StartPlaying(bytes, dataHash). (3) §4's file layout lacks the data hash, per-kind action fields (Select target, Cast slot/point, WalkTo point) and the per-tick held-input flag. The code is REPLAY_FORMAT 5. 0002 §7.4 also still says a recording starts from a state 'canonical at that moment'.

**Evidence:** src/core/replay.cpp line 15 (REPLAY_FORMAT = 5 and its history comment), lines 40–68, 135–170.

**Impact:** Anyone writing a replay tool or reviewing determinism from the doc gets the format and the order guarantee wrong.

**Recommendation:** Revise 0002.2 §3/§4 to format 5 (or point to replay.cpp's header comment as the format's single source), and delete 'sorted/canonical'.

**Verification:** REPLAY_FORMAT is 5. 0002.2 line 44 still says 'sorted, so canonical'. The doc has drifted from the code, as the finding says, and the drift is a known deferral.

### DESIGN-DOCS-27: Snapshot buffer count and instance size disagree within 0002

- **Severity:** low · **Verdict:** confirmed · **Category:** doc-inconsistency · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:536`

§5 (revised 2026-11-19) has five snapshot buffers. §13 still budgets '4 × about 4 MB'. The instance is 56 bytes plus a stable-ID array (8 B each) and a distance-field array since the glows chunk, so at 100,000 instances it is about 5 × 6.4 MB plus. The 'Note 2026-11-12 ... This text is the earlier design' is pasted three times instead of fixing the text.

**Evidence:** 0002 lines 163, 395, 411, 536; core/handover.hpp BUFFERS = 5.

**Impact:** The memory budget understates by about 2×, and readers have to chase spec Outcomes for current facts.

**Recommendation:** Update §5, §8.5, §8.7 and §13 with current sizes, and drop the repeated notes.

**Verification:** handover.hpp has BUFFERS = 5, but §13 line 536 still says '4 × about 4 MB'. The instance size is also stale: line 395 says 40 bytes, with a note that it is now 56. The doc carries a note rather than a fix.

### DESIGN-DOCS-28: Uncapped rendering by default, with vsync and frame cap settings unbuilt and S10 unrun

- **Severity:** low · **Verdict:** deliberate · **Category:** design-risk · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `docs/design/0002-engine-architecture.md:224`

Rendering takes MAILBOX, then IMMEDIATE, by default ('Vsync and a frame cap become player settings, off by default'), but core::Settings has neither, and S10 (whether uncapped presentation starves the logic thread, and how it behaves while hidden on niri) is still open. Phase 3 measured about 7,650 fps on an RTX 4090.

**Evidence:** src/core/settings.hpp (no vsync or cap fields); gfx/renderer.cpp PresentModeOrder.

**Impact:** Full GPU and CPU load in menus and on the world map: heat, fan noise and battery drain on laptops, and on the 4-thread minimum spec the main thread competes with the 2 workers.

**Recommendation:** Add vsync and frame-cap settings (accessible from every input, per the accessibility rule) before ship, and run S10 on the minimum spec. Consider capping by default when on battery.

**Verification:** Line 224 says vsync and a frame cap 'become' settings and that S10 decides on a default cap. S10 is recorded as waiting on other machines. settings.hpp has no vsync or cap field, which is consistent with the plan not being built yet. It is a known, scheduled gap. Whether to ship uncapped by default is still open.

### DESIGN-DOCS-29: Pause on focus loss is described as 'a setting' but is hard-wired

- **Severity:** low · **Verdict:** confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0002-engine-architecture.md:225`

§6: 'the game pauses itself when the window loses focus or is minimised (a setting)'. App pauses unconditionally, and there is no setting.

**Evidence:** src/app/app.cpp line 514; core/settings.hpp fields.

**Impact:** Players who stream, or who use a second monitor or assistive tools that steal focus, can't keep the game running. The project's accessibility-first rule favours making it an option.

**Recommendation:** Add the setting, or correct the doc.

**Verification:** app.cpp:511-514 pauses unconditionally on platform::PausesGame; settings.hpp has no pause option. Doc says '(a setting)'. Doc drift holds; the accessibility impact is speculative.

### DESIGN-DOCS-30: Replay recordings grow without limit in memory and are lost on close

- **Severity:** low · **Verdict:** deliberate · **Category:** robustness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/design/0002.2-replays.md:106`

§6 accepts that a recording still running when the game closes is lost. Replay keeps every Action (each about 64 bytes with a shared_ptr), a tick-start index and held input per tick in vectors, with no cap. A recording left on for days grows by tens of MB a day, and finishing it copies it all to one buffer on the logic thread.

**Evidence:** src/core/replay.cpp Record() and FinishRecording().

**Impact:** Low today (debug feature), but replays are meant for bug reports: the most valuable recordings, the ones running when a crash happens, are exactly the ones lost.

**Recommendation:** Stream ticks to disk in chunks (or flush on the crash handler), or at least cap the length with a log line.

**Verification:** 0002.2 section 6 lists 'a recording still under way at close is lost; F6 finishes and writes it' under 'Not in this piece'. Replays are an explicit debug feature. I did not check the unbounded growth in replay.cpp, but it is minor and follows from the same scoping.
