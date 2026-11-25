# Review: tests-rest (part 3)

### TESTS-REST-21: Reference shimmer thresholds are tuned magic numbers with no drift guard

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** test-quality · **Status:** known-deferred · **Reviewer confidence:** low
- **Where:** `tests/reference/reference_tests.cpp:2101`

MOST_CHANGE values 0.0026 (world), 0.0076 (roads), 0.0206 (clouds), 0.02 and MOST_ERROR 0.05 or 0.15 are tuned to current output and documented as 'a regression guard only'. Known-deferred notes say a drift guard for the measurement is missing.

**Impact:** A lavapipe or Mesa upgrade that moves the baseline slightly fails with no indication whether the renderer or the measure drifted. A tight threshold relative to current output gives little headroom.

**Recommendation:** Record the measured value next to each bound (the MESSAGE already prints it) and add headroom policy to AGENTS.md.

**Verification:** The bounds are tuned and documented as regression guards. The claim that the measured value should be recorded next to each bound is already met for at least the cited case: reference_tests.cpp:2099-2102 says 'Measured 0.00204 ... about 1.25 times that'. The headroom ratio is stated there too. The missing drift guard for the measure is a known deferral. Only the AGENTS.md headroom policy is absent.
