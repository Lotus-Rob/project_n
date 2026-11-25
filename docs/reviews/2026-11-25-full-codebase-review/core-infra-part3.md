# Review: core-infra (part 3)

### CORE-INFRA-21: FP environment check covers only rounding and FTZ/DAZ

- **Severity:** info (reviewer said low) · **Verdict:** confirmed · **Category:** determinism · **Status:** new · **Reviewer confidence:** low
- **Where:** `src/core/math.cpp:207`

On x86-64 only MXCSR bits 6 and 15 are checked or cleared. Unmasked exception bits (a library enabling FP traps) are not. On aarch64 only FPCR.FZ (bit 24) is cleared. FIZ and AH (FEAT_AFP, bits 0 and 1, which also flush inputs or change behaviour), DN (bit 25, default NaN, which changes NaN payloads that bitwise hashes see) and FZ16 are not checked or reset.

**Evidence:** `constexpr u32 FLUSH_BITS = 0x8040;` / `constexpr u64 FLUSH_BITS = u64{ 1 } << 24U;`

**Impact:** A library that changes these bits on a simulation thread could change results or trap without FloatingPointEnvironmentIsSet noticing. This is unlikely, but the function's contract ('what the simulation assumes') is broader than what it checks.

**Recommendation:** Compare and restore the whole control word: MXCSR's rounding, FTZ, DAZ and the exception masks against a canonical 0x1F80; on aarch64 compare FPCR against its expected value with FZ, FIZ, AH, DN and FZ16 cleared.

**Verification:** math.cpp:207 FLUSH_BITS is 0x8040 on x86 and bit 24 on aarch64. Only those bits are checked or cleared. The finding's own 'unlikely' and low confidence are fair. The function really does cover less than its contract wording suggests, but this is a hardening nicety, not a defect.

### CORE-INFRA-22: PN_PROFILE_MESSAGE evaluates its argument twice

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** correctness · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/profile.hpp:23`

`TracyMessage((text).data(), (text).size())` evaluates the expression twice. With `PN_PROFILE_MESSAGE(std::format(...))` or a function call, the text is built twice, and the pointer and length come from two different temporaries, which may differ if the producer isn't pure.

**Evidence:** profile.hpp:23.

**Impact:** Wasted work, and a possible length mismatch if the producer isn't pure. This kind of misuse shows up only in releasedbg builds (Gotchas).

**Recommendation:** Route it through an inline helper, e.g. `#define PN_PROFILE_MESSAGE(text) ::core::detail::ProfileMessage(text)`, with `inline void ProfileMessage(std::string_view t) { TracyMessage(t.data(), t.size()); }`. That also makes a bare literal work.

**Verification:** profile.hpp:23 does evaluate the argument twice, but both real callers (app.cpp:402, 410) pass a named constexpr string_view or a local std::string, so nothing is built twice. The mismatch risk is hypothetical. A helper would still be cleaner, and src/AGENTS.md:45 documents the literal restriction.

### CORE-INFRA-23: Stale NOLINT on SceneId: performance-enum-size doesn't fire on empty enums

- **Severity:** info · **Verdict:** unverifiable · **Category:** lint · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/manifest.hpp:38`

`}; // NOLINT(performance-enum-size): saved and sent with actions as two bytes` sits on the closing line, not on the declaration where the check would report. The identical empty `enum class SceneKey : u16 {}` (frame.hpp:103) and `StableId : u64` pass lint with no NOLINT, so the check doesn't fire on empty enums and this suppression is dead.

**Evidence:** manifest.hpp:37-38 vs frame.hpp:103-104.

**Impact:** Noise that makes a reader think a suppression is needed. It also departs from the rule that a NOLINT is a narrow last resort.

**Recommendation:** Remove it, or move the reason into the enum's doc comment.

**Verification:** manifest.hpp:37-38 has the NOLINT on the closing brace, and the empty SceneKey enum at frame.hpp:103 has none. Whether performance-enum-size fires on empty enums can only be decided by running clang-tidy, which I may not do. If it never fires, the suppression is dead noise.

### CORE-INFRA-24: Strings::text is a public mutable vector whose invariant the class relies on

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** api · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/strings.hpp:150`

Find/Add assume text holds exactly the STRINGS named entries first, with data strings indexed by m_index. `text` is public and mutable (the test resizes it directly), so any caller can break the link between m_index and text.

**Evidence:** strings_tests.cpp:42 `strings.text.resize(core::MOST_STRINGS - 1);`

**Impact:** An easy-to-misuse API: a stray push_back or resize desynchronises IDs and keys.

**Recommendation:** Make text private with a const accessor (and a test-only way to fill it), or document that only the data reader writes it.

**Verification:** strings.hpp:150 makes text a public mutable vector, and strings_tests.cpp:42 resizes it directly. Add() asserts on size, so the invariant is partly guarded. The remaining misuse risk is small.

### CORE-INFRA-25: Small dead code and doc nits in scope

- **Severity:** info · **Verdict:** confirmed · **Category:** maintainability · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/ui_colours.hpp:134`

(1) core::ToSrgb has no production caller (only tests): it waits for the accessibility menu. (2) ui_colours.hpp's file comment describes the accessibility menu as if it exists, while settings.hpp:52 says it doesn't yet. (3) DEFAULT_UI_COLOURS' doc ('Those that existed before the table are what they were, bit for bit') records history rather than a reason. (4) tests/unit/core/repeated_failure_tests.cpp is the only test without the explicit `core/cxx23.hpp` include (the PCH covers it). (5) game::TICKS_PER_DAY and DAYS_PER_MONTH re-alias core's calendar constants, giving one value two names.

**Evidence:** grep finds ToSrgb only in ui_colours.cpp and tests; settings.hpp:52; game/host.hpp:51-53.

**Impact:** Minor confusion and inconsistency.

**Recommendation:** Mark ToSrgb as kept for the menu, or drop it until then. Word the header in the future tense or as a contract. Replace the history note with a reason. Add the include for consistency. Use core::TICKS_PER_DAY directly.

**Verification:** Checked each point. (1) ToSrgb is declared at ui_colours.hpp:205 and defined at ui_colours.cpp:45, and grep over src finds no other caller. (2) The ui_colours.hpp file comment describes the accessibility menu in the present tense, while settings.hpp:52 says settings.yaml doesn't read the colours until the menu exists. (3) The doc at ui_colours.hpp:134 says 'Those that existed before the table are what they were, bit for bit', which is history. (4) repeated_failure_tests.cpp is the first file in `grep -L cxx23` output and has no cxx23 include; main.cpp is also listed, but it is not a test suite. (5) game/host.hpp:51-53 re-aliases TICKS_PER_DAY and DAYS_PER_MONTH from core. The alias comments say 'core's', so the duplicate names are deliberate and have a stated purpose. I checked tests/unit only for ToSrgb, which was enough to confirm the dead-code claim. I did not check the docs, AGENTS.md or the backlog for a recorded decision on any of the five points. The set is a collection of nits, all low impact.
