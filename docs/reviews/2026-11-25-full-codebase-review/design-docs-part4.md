# Review: design-docs (part 4)

### DESIGN-DOCS-31: Rule 14 points at a 'shortcut:' marker that doesn't exist, and the upstream report isn't tracked

- **Severity:** info · **Verdict:** confirmed · **Category:** doc-drift · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0001-repository-build-environment.md:365`

Rule 14 says the `-v` workaround 'is a `shortcut:` in project_n.py, to drop once XMake de-duplicates these requests (to report upstream)'. The comment in configure() has no `shortcut:` marker, and no backlog item tracks the upstream report.

**Evidence:** tools/project_n.py lines 684–687; `grep -n shortcut: tools/project_n.py` finds nothing.

**Impact:** The workaround can't be found by the shortcut convention, and it will never be removed.

**Recommendation:** Add the marker, or a backlog entry with the upstream issue link.

**Verification:** 0001 line 365 calls the -v workaround a `shortcut:` in project_n.py. configure() at lines 684-687 has a plain comment with no `shortcut:` marker, and grep for `shortcut:` in tools/project_n.py returned nothing. I did not check the backlog for an upstream-report entry.

### DESIGN-DOCS-32: §9 says each test main uses DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN alone, contradicting rule 19

- **Severity:** info · **Verdict:** confirmed · **Category:** doc-inconsistency · **Status:** new · **Reviewer confidence:** high
- **Where:** `docs/design/0001-repository-build-environment.md:449`

§9: 'one main, from DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN'. Rule 19 and tests/AGENTS.md say main.cpp must also define DOCTEST_CONFIG_IMPLEMENT because the PCH already included doctest, and every tests/unit/*/main.cpp does.

**Evidence:** tests/unit/core/main.cpp lines 1 and 4.

**Impact:** Following §9 alone gives a link without doctest's implementation.

**Recommendation:** Make §9 reference rule 19.

**Verification:** §9 line ~449 says each test main comes from DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN. Rule 19 in the same doc says main.cpp must also define DOCTEST_CONFIG_IMPLEMENT because the PCH has already included doctest. This is a real but trivial internal inconsistency.

### DESIGN-DOCS-33: Non-goals contradict planned work: modding and localisation

- **Severity:** info (reviewer said low) · **Verdict:** partially-confirmed · **Category:** doc-inconsistency · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/design/0002-engine-architecture.md:29`

0002 §1 lists 'scripting and modding, localisation beyond rendering Chinese and English text' as non-goals. 0001 §11.3 defines breaking changes from 1.0 to include 'mod/content formats', and the backlog plans a data-driven sweep for hot reload and mods, plus localisation to industry practice (per-locale files, BCP 47, ICU/Fluent).

**Evidence:** 0001 line 623; review-history 'Planned, not built'.

**Impact:** The architecture doc can't be used to judge whether a change keeps mods or locales possible.

**Recommendation:** Move modding and full localisation from non-goals to 'later goals, constraints now', e.g. data formats versioned, strings never in code.

**Verification:** Line 29 does list modding and localisation as non-goals. The conflict with 0001 §11.3 and the backlog plans is plausible. I did not verify those cited lines. The impact is a documentation clarity issue only.

### DESIGN-DOCS-34: DragonBones is still the planned skeletal runtime though its core is stale and S14 has never run

- **Severity:** info (reviewer said low) · **Verdict:** deliberate · **Category:** design-risk · **Status:** contradicts-recorded-decision · **Reviewer confidence:** medium
- **Where:** `docs/design/0002-engine-architecture.md:457`

§8.13 itself records that the core's last substantive change was in 2018, the editor's site didn't answer, and its successor (LoongBones) is unconfirmed. A12 still commits heroes and bosses to DragonBones, with S14 unrun and phase 7 approaching.

**Evidence:** 0002 lines 455–457, 551.

**Impact:** Phase 7 could find that no maintained editor exports data the 5.6 runtime reads, after the art pipeline has been planned around it.

**Recommendation:** Run S14 (or a paper check of LoongBones export compatibility) before phase 7's spec. Name a fallback in the doc: an MIT runtime such as rive-runtime (whose editor, unlike Spine's licence, doesn't burden AGPL source recipients), a small in-house bone format, or baked sheets for all.

**Verification:** §8.13 line 457 openly records the stale core, and S14 (line 551) is a planned spike scheduled in phase 7 (line 569). That makes it a recorded, spike-gated risk, not an oversight. Adding a fallback to the doc is a reasonable suggestion, but nothing is wrong yet.

### DESIGN-DOCS-35: The contraction decision is untested on x86-64 and silently changes if the ISA baseline rises

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** determinism · **Status:** new · **Reviewer confidence:** medium
- **Where:** `docs/design/0002-engine-architecture.md:267`

§7.4 leaves -ffp-contract at its default, reasoning that contraction is fixed per binary. On the x86-64 baseline (no -march), compilers cannot emit FMA at all, so contraction only happens on macOS arm64. If a later SIMD or CPU-baseline item (deferred in the backlog) adds -march=x86-64-v3, results change between builds, and replays and saves already break across builds by design. The decision is fine, but its consequences are undocumented.

**Evidence:** xmake.lua: no -march or -ffp-contract flags.

**Impact:** Surprises in cross-build reference data; for example, test expectations computed on Linux may differ on macOS arm64.

**Recommendation:** Note in §7.4 that x86-64 builds have no FMA and arm64 builds fuse, and that raising the ISA baseline is a deliberate determinism-affecting change.

**Verification:** xmake.lua has no -march or -ffp-contract flags, so the facts hold. Section 7.4 already says contraction is fixed per binary and to revisit if cross-build matching is needed, and it states that cross-platform results may differ. Only the explicit x86-64-has-no-FMA note is missing. Minor doc nicety.

### DESIGN-DOCS-36: core::Action carries a shared_ptr<GameData> in every action, including recorded and queued ones

- **Severity:** info · **Verdict:** partially-confirmed · **Category:** design-smell · **Status:** new · **Reviewer confidence:** low
- **Where:** `docs/design/0002.3-input-data-scenes.md:60`

0002.3 §3 grows Action with `std::shared_ptr<const GameData> data` for ReloadData only. Every Action, empty or not, is now a non-trivial type with a refcount: in the handover's input queue, in replay's m_actions, and in each tick's span.

**Evidence:** src/core/frame.hpp lines 514–529.

**Impact:** Small copies cost more, Action is no longer trivially copyable or comparable, and a debug-only payload sits in every recorded action.

**Recommendation:** Send data reloads on their own handover slot (like HeldInput or the view), keeping Action plain data.

**Verification:** frame.hpp:520 does have shared_ptr<const GameData> data in Action, and 0002.3 line 61 documents it as the chosen design. The non-trivial-copy cost is real but small, since Actions are low-rate input events. It is a design opinion, not a defect.
