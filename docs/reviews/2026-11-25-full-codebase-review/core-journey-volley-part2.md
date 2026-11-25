# Review: core-journey-volley (part 2)

### CORE-JOURNEY-VOLLEY-11: Tests miss the contracts the defects above live in

- **Severity:** low · **Verdict:** partially-confirmed · **Category:** test-coverage · **Status:** new · **Reviewer confidence:** high
- **Where:** `tests/unit/core/volley_tests.cpp:518`

volley_tests checks the even split only for count 1003 over 10 waves, and the proportional split only for counts 1000/1003 on the shipped rings, so they miss the truncation bug (e.g. 22/22, or 90 over 10 equal rings). There are no tests for waves > count or ringRadius 0 with ringStep > 0 (an empty inner wave), nor for CommitTicksFor's saturation path (a huge or NaN radius gives u32 max). journey_tests: MemberOffset beyond ring 1 is untested (the header's '41.6 to 48 units' claim for members 7 to 18). So are a stay with a partial last day (allowed for loaded whole journeys, handled by StayWhere and StayWalked), a KeptWalk over a journey of more than KEPT_PARTS parts (the `index >= kept.tables.size()` path), and WalkedAt across a zero-day stay.

**Evidence:** volley_tests.cpp:488-528 (counts {1000, 1003}); journey_tests.cpp:308-325 (three members only); no test builds a Stay whose span isn't whole days or a journey with four or more parts.

**Impact:** The wave-split defect passes the suite today. Changes to the kept walk past three parts, or to ring-2 offsets, would go unnoticed.

**Recommendation:** Add representative samples: count == waves with no ring, 90/10 equal rings, waves > count; CommitTicksFor(1e30F) == u32 max; members 7 and 18 at 48 and ~41.6; a 2.5-day stay; a four-part journey through the kept overload compared bit for bit with the fresh walk.

**Verification:** CommitTicksFor is tested at volley_tests.cpp:1208-1212 and :1385, including radius 0 giving MIN_COMMIT_TICKS. No test covers a huge or NaN radius saturating to u32 max. KeptWalk is tested in journey_tests.cpp (around lines 915-983), and KEPT_PARTS is 3 (journey.hpp:241). I did not check whether any of those tests uses more than three parts, partial-day stays, or ring-2 member offsets. I could not confirm the wave-split truncation defect, because it is a separate finding that I was not given.

### CORE-JOURNEY-VOLLEY-12: Journey::parts documented 'never empty' while the API returns and handles journeys without parts

- **Severity:** info · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/journey.hpp:95`

Journey::parts says 'Its legs and stays in order; never empty.' Yet Rebuild() returns 'a journey without parts' on failure, and EndTick/StartTick/Where/Underway all document behaviour for 'a journey without parts'.

**Evidence:** journey.hpp:95 vs :139-143, :155, :316.

**Impact:** Readers cannot tell whether an empty journey is a valid state; callers may skip the empty check or add needless ones.

**Recommendation:** Say that parts is empty only for a failed Rebuild (a sentinel), or return std::optional<Journey> from Rebuild and keep 'never empty' true.

**Verification:** journey.hpp:95 says 'never empty', while Rebuild() and the other functions document handling a journey without parts. This is a doc inconsistency.

### CORE-JOURNEY-VOLLEY-13: Stay drift seeds are u32 drawn per journey: same-seed stays drift identically

- **Severity:** info · **Verdict:** unverifiable · **Category:** design · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/journey.hpp:62`

Stay::seed and VisitPlan::seed are u32, used as the full seed of core::Random(stay.seed, 0, day, STAY_PURPOSE, draw). Day points depend only on (seed, day), not on the start tick or the member. Two stays with the same seed drift through the same offsets on the same day index.

**Evidence:** journey.cpp:51 `Random(stay.seed, 0, static_cast<u64>(day), STAY_PURPOSE, draw)`.

**Impact:** With thousands of journeys a month, u32 collisions are rare but possible. Two travellers staying in one city with colliding seeds and aligned days would move as one stacked sprite. Purely visual.

**Recommendation:** Mix the leader's StableId (or the departure tick) into the draw's key, e.g. stableId = member id and draw = day*2+axis, or widen the seed to u64.

**Verification:** I did not re-read the Random keying. The finding's own evidence (journey.cpp:51) shows the seed is the only per-stay key. A u32 collision is possible but rare and cosmetic. Whether it matters is a design call.

### CORE-JOURNEY-VOLLEY-14: A staying traveller is 'moving' for the whole stay at a drift of ~0.5 units/tick

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** design · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/journey.cpp:176`

StayWalked reports moving whenever the day's stretch has nonzero length. Day points stray up to 0.34 tile (~87 units) per 180-tick day, so a staying traveller counts as moving all stay at about 0.5 units per tick, an eighth of road pace. The walk clip then plays at a crawl through every stay.

**Evidence:** journey.cpp:176 `walked.moving = walked.moving || (stretch.length > 0.0 && tick <= dayEnd);`

**Impact:** Possibly odd presentation: city crowds shuffle in slow motion rather than standing or walking. A design call, not a bug.

**Recommendation:** Decide on purpose. Options are a minimum speed below which the walker counts as standing, or a drift that walks briefly then stands (an ease with a pause).

**Verification:** journey.cpp:176 matches the quote: moving is true whenever the stretch length is above 0 and tick <= dayEnd. I did not measure the drift speed. It is a presentation decision, not a defect.

### CORE-JOURNEY-VOLLEY-15: journey.hpp includes calendar.hpp although only journey.cpp uses it

- **Severity:** info · **Verdict:** confirmed · **Category:** includes · **Status:** known-deferred · **Reviewer confidence:** high
- **Where:** `src/core/journey.hpp:10`

The header declares nothing that needs core/calendar.hpp; TICKS_PER_DAY is used only in journey.cpp.

**Evidence:** journey.hpp:10 `#include "core/calendar.hpp"`.

**Impact:** Extra rebuild coupling.

**Recommendation:** Move the include to journey.cpp.

**Verification:** grep of journey.hpp shows calendar.hpp is included at line 10 and TICKS_PER_DAY appears only in a comment at line 53. The header uses nothing else from it. The include can move to journey.cpp.

### CORE-JOURNEY-VOLLEY-16: Doc comments on the volley's random purposes are grouped so Doxygen attaches them to the wrong constant

- **Severity:** info · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/volley.cpp:13`

The block comment for the whole purpose group (lines 13-14) runs straight into PURPOSE_DEPART's own `///` line, so it becomes part of PURPOSE_DEPART's documentation. PURPOSE_SCATTER_REACH has no doc of its own; it relies on PURPOSE_SCATTER_ANGLE's.

**Evidence:** volley.cpp:13-16 three consecutive `///` lines before `PURPOSE_DEPART`; :25-26.

**Impact:** Cosmetic: misleading generated docs (anonymous namespace, so low).

**Recommendation:** Make the group comment a plain `//` block or separate it with a blank line, and give PURPOSE_SCATTER_REACH its own line.

**Verification:** volley.cpp:13-16 has the group comment as two `///` lines running directly into PURPOSE_DEPART's own `///` line. Doxygen would attach all three lines to PURPOSE_DEPART. PURPOSE_SCATTER_REACH at line 26 has no doc line. The constants are in an anonymous namespace, so the impact is cosmetic.

### CORE-JOURNEY-VOLLEY-17: Volley ticks are u64 simulation ticks, journey ticks are i64 world ticks: two tick types in core

- **Severity:** info · **Verdict:** confirmed · **Category:** api-design · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/volley.hpp:200`

Volley::startTick and VolleyTarget::diedTick are u64 simulation ticks; Leg/Stay ticks are signed i64 world ticks. Both are correct for their clocks, but nothing in the type system separates the two clocks.

**Evidence:** volley.hpp:200 `u64 startTick`; journey.hpp:37 `i64 startTick`.

**Impact:** Mixing a world tick into a volley (or the reverse) compiles silently with sign conversions, a future footgun as combat comes to the world map.

**Recommendation:** Consider strong tick types (e.g. SimTick, WorldTick) in core, or at least name the clock in each field's doc.

**Verification:** Checked the code. volley.hpp:200 has `u64 startTick` and :239 has `u64 diedTick`. journey.hpp:37 and :58 use `i64 startTick`. Both are plain integers, and I found no SimTick or WorldTick strong type in core. The claim is factually accurate. It is a low-confidence, speculative API-design note, and it rests on no current bug. I did not check docs or the backlog for a recorded decision. Info severity is right.
