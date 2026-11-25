# Review: core-persist (part 3)

### CORE-PERSIST-21: Save and replay files share one magic, so file kind is told apart only by format numbers

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** versioning · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/save_file.cpp:13`

Replays are written through the same WriteSaveFile container (MAGIC 'PN_SAVE'). A replay placed in the save slot is refused only because Logic::Restore's SAVE_FORMAT (1) differs from the replay's leading REPLAY_FORMAT (5). A save played as a replay is refused because its leading u32 isn't 5. If SAVE_FORMAT is ever raised to the replay's format number (or the reverse), a file of the wrong kind passes the first check and is parsed as the other.

**Evidence:** save_file.cpp:13; logic.cpp:13 SAVE_FORMAT=1; replay.cpp:15 REPLAY_FORMAT=5.

**Impact:** Type confusion is only coincidentally refused. Error messages say 'malformed or from another version' instead of 'this is a replay'.

**Recommendation:** Give the container a kind field (save or replay), or use distinct magics, checked by Saves (each Saves instance knows its kind).

**Verification:** Shared MAGIC and container confirmed (save_file.cpp:13, WriteSaveFile used by both). Type confusion is refused today because the leading u32 differs (SAVE_FORMAT 1 vs REPLAY_FORMAT 5). Hypothetical future collision only; the nested checks (version string, hash, simulation Load) would also likely fail. Hardening idea, not a defect.

### CORE-PERSIST-22: Replays are stamped only with the semantic version, so a different build of the same version reports a false divergence

- **Severity:** info (reviewer said low) · **Verdict:** deliberate · **Category:** design · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `src/core/replay.cpp:16`

The replay header holds REPLAY_FORMAT, PN_GAME_VERSION ('0.1.0' for all builds since the start) and the data hash. Replays are deterministic only within one binary (recorded decision), and 0002.2 accepts that 'two builds of one version that differ are caught by the first hash check'. In practice every dev commit, compiler or mode shares the version, so playing a colleague's or CI's replay logs PN_LOG_ERROR 'the replay diverged' instead of 'recorded by another build'.

**Evidence:** replay.cpp:16, 160-163, 199-206; build_info.hpp.in has only PN_GAME_VERSION; docs/design/0002.2-replays.md:86-87.

**Impact:** Replays shared as bug reports produce misleading divergence errors, and a real divergence regression can't be told apart from a build mismatch.

**Recommendation:** Add a build ID (git commit + toolchain + mode, from build_info.hpp) to the header. Warn or refuse on a mismatch with a clear message, while still allowing playback for diagnosis.

**Verification:** docs/design/0002.2-replays.md:16 says 'Same binary only; across builds nothing is promised' and lines 86-87 say two builds of one version are caught by the first hash check. The finding's improvement (build ID for a clearer message) is a fair enhancement, but the status is a recorded trade-off, not a bug.

### CORE-PERSIST-23: Release builds rely on .at() throws, and UB shifts for >64 fields, where only PN_ASSERT guards

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** medium
- **Where:** `src/core/delta.cpp:8`

FieldBytes::Field and MarkChanged use .at(), which throws std::out_of_range in release when the asserts are compiled out, contrary to 'our code throws nothing'. The <= 64-field limit is a static_assert per archive() call and a PN_ASSERT across calls. A type whose Fields() makes several archive calls totalling more than 64 fields (combat.hpp Caster already calls archive in a loop) would, in release, compute `u64{1} << field` for field >= 64 in ChangedFields, which is UB, and mis-mask in LoadChanged.

**Evidence:** delta.cpp:7-21, 94-101; delta.hpp:65-67; combat.hpp:101-106.

**Impact:** Latent: today's WorldComponents have at most 4 fields.

**Recommendation:** Use a compile-time field count (e.g. a constexpr FieldCount via a counting archive in a consteval context) with static_assert, and use operator[] after asserting rather than .at().

**Verification:** delta.cpp:8-9 uses .at() and ChangedFields shifts by field. The static_assert per archive call (delta.hpp:65,103) and PN_ASSERT at line 67 guard the 64-field limit in debug builds. In release, more than 64 fields across multiple archive calls would be UB, but this is latent: no current type comes near it. Valid hardening, no present defect.

### CORE-PERSIST-24: Saves doc overclaims 'the logic thread never waits on the disk'

- **Severity:** info · **Verdict:** confirmed · **Category:** docs · **Status:** new · **Reviewer confidence:** high
- **Where:** `src/core/saves.hpp:5`

Saves::Write calls Jobs::Background, which first calls WaitForBackground(). If a previous save, or a replay being written through the other Saves on the same Jobs, is still running, the logic thread blocks until that disk write finishes. Read waits for any background job, then decompresses and checksums synchronously. saves.hpp also uses std::move without including <utility> (headers include what they use).

**Evidence:** saves.hpp:5-7, 24-27; jobs.cpp:206-210.

**Impact:** A misleading contract: a quick F5 after F6 (a long replay write) stalls a tick.

**Recommendation:** Reword it to 'returns at once unless a previous background write is still running', and include <utility>.

**Verification:** saves.hpp:5 claims the logic thread never waits on the disk, but Jobs::Background (jobs.cpp:206) first calls WaitForBackground(), so a second write while one is pending blocks. Also std::move is used with no <utility> include (saves.hpp includes only cstddef, expected, filesystem, vector). Doc wording nit.
