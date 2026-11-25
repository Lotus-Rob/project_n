# Review: x-test-strategy (part 2)

### X-TEST-STRATEGY-11: Test guidance (about 20 samples, no sweeps) leaves exhaustive checks to fuzzing, which does not reach the game rules

- **Severity:** low · **Verdict:** deliberate · **Category:** test-strategy · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/AGENTS.md:1`

Breadth is delegated to fuzzing, but the three fuzz targets cover parsers and the save loader, not the game rules. There is no property-based or randomized rule test: combat rules (damage and affliction math), journey and route properties (path continuity, monotonic arrival), calendar, fog union, road curves. The guidance forbids sweeps in unit tests, so these properties are checked on hand-picked samples.

**Impact:** Edge cases like overflow in damage, negative or NaN positions mid-sim, and journeys that cross map edges are found only by luck. As the game design adds cultivation realms, breakthroughs and items, the combinatorial space grows.

**Recommendation:** Add a property-test layer (seeded random generators run for a bounded count, deterministic, with the seed printed on failure) for core math, journey, volley and fog invariants. Keep it separate from the small unit tests, and cap it to a second or two.

**Verification:** tests/AGENTS.md line 9 says 'breadth is fuzzing's' and limits unit tests to about 20 samples. The absence of a property-test layer follows from that rule. Whether rule-level fuzzing is worth adding is a design opinion; the facts are correct.

### X-TEST-STRATEGY-12: Windows and macOS native jobs run tests but not the reference pictures or the replay hash across presets

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** cross-platform · **Status:** new · **Reviewer confidence:** low
- **Where:** `.github/workflows/ci.yml:305`

The native Windows and macOS jobs run `test --preset ...` (release and ASan) only. Reference pictures, the bench, fuzz seeds and the container Wine run apply to Linux alone. Cross-platform replay determinism is out of scope by decision, but no test says that a save written on one platform loads on another (endianness is fixed, but struct layout, size_t width, and float-to-text formatting in saves are not checked against a committed binary save).

**Impact:** A save written on Windows opened on Linux (or shared on a bug report) may refuse or differ without any test noticing. Atlas and shader outputs are committed, so those are fine.

**Recommendation:** Commit one binary save file generated at the current format (the fuzz seeds are already this) and add a test that loads each seed and asserts the loaded hash equals a literal, run on every OS. This tests save portability without a cross-platform determinism goal.

**Verification:** ci.yml around line 305 runs only 'test --preset windows-msvc' on Windows. Reference pictures and fuzz seeds are Linux-only (tests/AGENTS.md line 20, design 0001 rule 24). I found no cross-OS test that loads a committed binary save and asserts a literal hash. The risk is real but small, since the format is explicit. Not verified how save floats are written.

### X-TEST-STRATEGY-13: No golden hash: determinism regressions that hold within one binary are invisible across commits

- **Severity:** info (reviewer said medium) · **Verdict:** deliberate · **Category:** determinism · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `tests/unit/game/replay_tests.cpp:164`

Every replay test records and replays inside the same process and compares hashes only with itself. No test pins a state hash from a committed replay or save to a known value, and replays from another build are refused by version. Within-binary determinism is the recorded decision, but a change to a hash-relevant default, an unordered iteration, or a changed RNG purpose order cannot be detected by comparing with the previous commit.

**Impact:** Silent behavioural change in simulation, such as a different NPC count or loot roll after a refactor, passes every test, as long as it is self-consistent. Balance work and bug reports that cite a seed cannot be reproduced.

**Recommendation:** Keep one 'canary' test per scene that asserts a committed literal StateHash/WorldHash after a fixed run, with a documented regeneration step like `reference --update`. It breaks per compiler, so gate it to linux-gcc/clang release, or accept a per-preset table. Also add a test that fails if an unordered container is iterated for the hash.

**Verification:** The sweep-up backlog (line 14, item 4b) says design doc 0002 promises determinism within one binary for any worker count, and 'compare runs of one build, never pinned hashes'. The user chose this. The finding is true but contradicts a recorded decision, and pinned hashes would break per compiler.

### X-TEST-STRATEGY-14: Hash-collision and comparison risk: replay tests compare only 64-bit hashes of states

- **Severity:** info · **Verdict:** unverifiable · **Category:** determinism · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/unit/game/replay_tests.cpp:164`

Replay and worker-count comparisons compare StateHash and WorldHash (64-bit), not the states. A divergence in a field the hash does not cover (presentation-only or derived caches such as the leg-walk cache, and fog, if not hashed) would pass.

**Impact:** Silent desync between worker counts in unhashed fields, such as publish-side snapshots, which the renderer then draws. The 'publishing travellers on 1 and 4 workers' test is a partial cover.

**Recommendation:** Add a test that asserts the hash covers every serialized component (compare hash coverage with the save writer's field list), or compare saves byte for byte as save_exactness does, in the 1 versus 4 worker cases too.

**Verification:** Replay and save tests compare StateHash and WorldHash (e.g. save_exactness_tests.cpp lines 60-85). Whether the hash covers every serialised field needs the hash code. Byte-for-byte save comparison already exists in save_exactness. Speculative.

### X-TEST-STRATEGY-15: Test design doc and guidance describe stale gate numbers and heavy prose

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/AGENTS.md:1`

tests/AGENTS.md is far over the ~100-line target in the root AGENTS.md rules: the reference bullet alone is a multi-hundred-word paragraph enumerating every case. It restates the pictures list, which then drifts from reference_tests.cpp, while the headless bench gates are quoted as 'phase 4' numbers.

**Impact:** Contradicts the repo's own 'one line per rule' rule and is likely to go stale. Readers cannot find the rule among case descriptions.

**Recommendation:** Move the per-case catalogue into a header comment in reference_tests.cpp (or a docs file) and keep one line of rules in AGENTS.md.

**Verification:** tests/AGENTS.md is only 46 lines, so the 'far over ~100 lines' claim is wrong. But line 20 (the reference bullet) is about 5.8k characters, which breaks the 'one line per rule' spirit. Lines 19 and 28 do quote 'phase 4' gates. The catalogue drifting from reference_tests.cpp is plausible and the recommendation is sound. This is a doc nit.
