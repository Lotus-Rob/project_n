# Review: tests-core (part 2)

### TESTS-CORE-11: Core Valid() predicates and Vec2/view_limits/assert have no direct core tests

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** missing-test · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/core/glow_tests.cpp:140`

glow, volley, and seal-key validity are tested in core, but CombatRules::Valid (combat_rules.hpp:90), Ability/AbilityTable Valid (abilities.hpp), SealLook/Seal Valid beyond the key, effects.hpp's Valid chain, Vec2 operators and IsFinite, and view_limits constants have no tests under tests/unit/core. assert.cpp (abort path) is untestable as written.

**Evidence:** grep over tests for core/combat_rules finds nothing; CombatRules is touched only via data/abilities_tests and game tests.

**Impact:** These predicates gate untrusted YAML and saved data; they are covered only indirectly through parser tests, so a predicate loosening that the YAML fixtures do not hit goes unnoticed, and core_tests alone does not document them.

**Recommendation:** Add one case per Valid() with a boundary each (0 ticks, NaN reach, min>max), and a test of Vec2 arithmetic and IsFinite.

**Verification:** grep found no CombatRules or AbilityTable in tests/unit/core, and no direct Valid() tests there beyond glow (glow_tests:137-144) and the delta test fixture. The grep for other test directories failed, so the claim that data/abilities_tests covers them indirectly is unchecked. The gap is plausible. Vec2 and view_limits were not checked in detail.

### TESTS-CORE-12: Refused delta/order loads never checked for leaving the state untouched or half-applied

- **Severity:** info (reviewer said low) · **Verdict:** deliberate · **Category:** missing-test · **Status:** new · **Reviewer confidence:** low
- **Where:** `tests/unit/core/delta_tests.cpp:246`

The malformed-delta SUBCASEs only assert Loads(...) is false; none asserts the target state's hash or contents after a refusal. Saves::Write failure paths and Write ordering under a failing disk are also untested.

**Evidence:** CHECK_FALSE(Loads(Bytes(...), state, Short{})) with no check of state afterwards.

**Impact:** If LoadDomainDelta partially applies a bad delta (for example removes then fails on a later section), callers that keep using the state after a refusal would run a corrupt world; whether callers discard the state is only knowable from game tests.

**Recommendation:** After each refusal compare Hash(state) with Hash of a fresh Base() or document and test that a failed load leaves the state to be discarded.

**Verification:** src/game/host.cpp:~264 documents that worlds read from a failed archive are to be thrown away, so a refused load is not required to leave the state untouched. The finding itself says the callers' behaviour is the only open question, and that behaviour is documented. A test of the documented contract would be optional. The Saves::Write failure paths were not checked.

### TESTS-CORE-13: Misleading or time-dependent test names and overloaded counting asserts

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** naming · **Status:** new · **Reviewer confidence:** medium
- **Where:** `tests/unit/core/ui_colours_tests.cpp:45`

'the defaults are today's colours', 'the new elements' defaults ...' use time-relative wording; delta, journey and handover tests use `differ`/`strays`/`mismatches` counters asserted ==0, which hides which sample failed. 'starting a background job waits for the last one first' only checks order after both started.

**Evidence:** TEST_CASE("the defaults are today's colours"); CHECK(differ == 0); CHECK(mismatches == 0);

**Impact:** Minor: harder diagnosis on failure and wording that goes stale.

**Recommendation:** Rename to name the behaviour ('the shipped default colours'); on mismatch CAPTURE the first offending sample before the aggregate check.

**Verification:** The test name 'the defaults are today's colours' exists at ui_colours_tests.cpp:45, and the wording is time-relative. The aggregate counters asserted ==0 were not verified. This is a cosmetic nit.
