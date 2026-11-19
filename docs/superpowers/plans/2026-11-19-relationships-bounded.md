# Relationships bounded Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Each NPC keeps at most 10 relationships (50 once met), forgetting the ones it met longest ago, so saves, memory and the month step stay bounded over any length of play.

**Architecture:** `game::Relationship` gains a `lastMet` month that `Relationships::Merge` stamps. A new `Relationships::Trim`, run by `MonthStep::Run` right after the merge, cuts every holder over its limit down to its most recently met subjects in one linear pass. The limits are two constants in `game/npcs.hpp`, passed to `Trim` as a defaulted struct. The world save's `WORLD_VERSION` goes from 5 to 6.

**Tech Stack:** C++23, EnTT through `core/ecs.hpp`, doctest, XMake through `pixi run project_n`.

**Spec:** `docs/superpowers/specs/2026-11-19-relationships-bounded-design.md`

## Global Constraints

- `RELATIONSHIP_LENGTH = 10`, `MET_RELATIONSHIP_LENGTH = 50` (std::size_t, `game/npcs.hpp`), the journal's numbers.
- Keep order when a holder is over its limit: most recent `lastMet` first, then the higher `opinion`, then the lower subject ID. The table stays sorted by (holder, subject).
- A holder's limit is the met one when it has `Met`, in whichever domain it is (an NPC the arena brought into the active domain included). Being the subject of others' edges never changes a limit.
- Deterministic within one binary and the same for any worker count. `Trim` runs on the logic thread after the join, never inside a job chunk.
- A load never refuses a holder for having more edges than the limits. It does refuse an edge whose `lastMet` is past the loaded calendar's `months`.
- `WORLD_VERSION` 5 → 6, with its history comment in `game/host.cpp`. `GENERATOR_VERSION` and `ARENA_FORMAT` stay as they are.
- Code comments say why and never cite the spec, plan or tasks (`lint` checks). Commits follow the git-commit skill: `fix(game)` for the bound, since it fixes the save failing near 360 h, and `docs(design)` for the Outcome. AGENTS.md lines change in the same commit as the behaviour they describe.

## Review Focus

1. A met NPC brought into the active domain (the arena's `Activate`) still gets the met limit. Test in Task 1 (the met-limit case uses an activated holder).
2. A month step stopped before it starts merges and trims nothing. The existing "a month step stopped before it starts changes nothing" case must still pass with `Trim` inside the same guard as `Merge`. Task 1, Step 4.
3. A holder at exactly its limit loses nothing (the boundary). Test in Task 1 (first case).
4. A version-5 save is refused like any other version, and the four save fuzz seeds are written again in the new layout, so they exercise loading and not only the refusal. Task 1, Step 6.
5. The month step at `LAST_MONTH`, where `months` no longer rises: the stamp stays `months` and the load check (`lastMet <= months`) still holds. No test needed: it follows from stamping `world.months` and checking `<=`.

## Waves

| Wave | Tasks | Needs |
|---|---|---|
| 1 | Task 1 | nothing |
| 2 | Task 2 | Task 1 on the branch; main checkout; runs alone (measurement) |

---

### Task 1: Last-met months, the limits, the trim and the save version

`review: full` (determinism, saves) · `implementer: sonnet`

**Files:**
- Modify: `src/game/npcs.hpp` (`Relationship`, `Relationships`, the limit constants, `RelationshipLimits`, `MonthStep::Run`)
- Modify: `src/game/npcs.cpp` (`Merge`, new `Trim`, `Load`, `MonthStep::Run`)
- Modify: `src/game/host.cpp` (`WORLD_VERSION` and its comment, the `Run` call in `Host::AdvanceCalendar`, both `relationships.Load` calls in `ReadWorld` and `ReadScratch`)
- Modify: every existing caller of `Merge`, `Run` and `Load` in `tests/unit/game/` (`npcs_tests.cpp`, `save_tests.cpp`; find the rest with the `LSP` tool's `findReferences`)
- Modify: `tests/fuzz/corpus/save/{fight,world,delta,scratch}` (written again)
- Modify: `src/AGENTS.md` (the "relationships and journals whole" passage: relationships are kept to the limits, most recently met first), `docs/design/0002-engine-architecture.md` (the data-layout line on the edge table: one more clause naming the per-NPC bound)
- Test: `tests/unit/game/npcs_tests.cpp`

**Interfaces:**
- Produces:
  - `struct Relationship { core::StableId holder; core::StableId subject; f32 opinion; u64 lastMet = 0; }`, with `Fields` listing all four (28 raw bytes, so `Load`'s `EDGE_BYTES` becomes 28)
  - `inline constexpr std::size_t RELATIONSHIP_LENGTH = 10;` and `inline constexpr std::size_t MET_RELATIONSHIP_LENGTH = 50;`
  - `struct RelationshipLimits { std::size_t kept = RELATIONSHIP_LENGTH; std::size_t keptOnceMet = MET_RELATIONSHIP_LENGTH; };`
  - `void Relationships::Merge(std::span<const std::pair<core::StableId, core::StableId>> changes, u64 month);` (sets `lastMet = month` on every edge it adds to)
  - `void Relationships::Trim(const core::State& state, RelationshipLimits limits = {});`
  - `void Relationships::Load(core::Reader& archive, u64 months);`
  - `void MonthStep::Run(core::State& state, Relationships& relationships, core::Jobs& jobs, u64 stateCounter, u64 month, const std::stop_token& stop = {});`

- [ ] **Step 1: Write the failing tests** in `tests/unit/game/npcs_tests.cpp`. The NPCs come from `game::SpawnNpc` on a `core::State(game::DOMAINS, seed)`, so their IDs are 1, 2, 3…, and edges come from `Merge` with literal pairs sorted by (holder, subject):
  - `"a holder past its limit forgets the subjects it met longest ago"`: holder 1 is met by subjects 2…11, one per `Merge`, months 1…10. After `Trim(state)`, all 10 are kept (the boundary). Then subject 12 meets it in month 11, and after `Trim(state)`, `Of(1)`'s subjects are 3…12 in order, with subject 2 gone.
  - `"a met holder keeps the met number, in either domain"`: limits `{ .kept = 2, .keptOnceMet = 4 }`. Holders A (with `Met`) and B (without) are each met by five subjects in months 1…5. A is then `Activate`d. After `Trim(state, limits)`, A keeps its 4 most recently met and B its 2.
  - `"a holder over its limit keeps the stronger tie, then the lower ID, among those met the same month"`: limits `{ .kept = 2 }`. Holder 1 is met in month 3 by subjects 5, 4 and 3, and by 5 a second time (opinion 2). `Trim` keeps subjects 3 and 5.
  - `"a lowered limit trims a holder the month left alone"`: holder 1 holds 5 edges from months 1…5. `Trim(state, { .kept = 3 })` with no `Merge` before it leaves the 3 from months 3…5.
  - `"merging stamps each edge it adds to with the month"`: edges merged in month 1, and one of them again in month 4. Its `lastMet` is 4 and the others' is 1.
  - `"month steps keep every NPC within its limit"`: with the `Stepped` helper (updated for the new `Run`), enough NPCs in one cell and enough months that the largest `Of(holder).size()` equals `RELATIONSHIP_LENGTH` exactly. Pick the numbers so it does under the fixed seed, and keep it under about a second in debug.
  - `"relationships load as saved, last-met months included"`: a table saved to a `core::Writer` and loaded with `months` equal to the latest `lastMet` compares equal field by field.
  - `"a load keeps a holder over the limit"`: a table where one holder has 12 edges loads with all 12.
  - `"a relationship last met after the calendar is refused"`: loaded with `months` one below the latest `lastMet`, `archive.Failed()` is true and the table keeps its earlier edges.

- [ ] **Step 2: Run them to see them fail**

Run: `pixi run project_n test --suite game_tests --test npcs`
Expected: the build fails (no `Trim`, no `lastMet`, old `Merge` and `Load` signatures).

- [ ] **Step 3: Implement** the interfaces above in `src/game/npcs.{hpp,cpp}`, with `///` docs saying the limits follow the travel journal's. `Trim` is one pass over the table: a holder with at most `kept` edges is copied as it is, with no `Met` lookup. Only for a longer one look up `Met` (`state.DomainOf(holder)`, then that domain's `Find` and `all_of<Met>`), choose the edges to keep by the keep order in Global Constraints, and write them back in subject order. Reuse `m_merged` as the output buffer, as `Merge` does. `Load` takes `months` and fails on `lastMet > months`. It never fails on a holder's edge count.

- [ ] **Step 4: Wire it into the month step and the host.** `MonthStep::Run` calls `relationships.Merge(m_merged, month)` and then `relationships.Trim(state)`, both after the `stop` check, so a stopped step changes nothing. `Host::AdvanceCalendar` passes `world.months`. `ReadWorld` and `ReadScratch` pass `clock.months` to `relationships.Load`. Set `WORLD_VERSION` to 6 and add `6: each relationship's last-met month` to its history comment. Update the other callers' signatures.

- [ ] **Step 5: Run the suite**

Run: `pixi run project_n test --suite game_tests`
Expected: every case passes, including the save and determinism cases ("a month step gives the same world with one worker and with many", "a month step stopped before it starts changes nothing", save_exactness).

- [ ] **Step 6: Write the save fuzz seeds again** (the layout changed). Run `pixi run project_n test --target game_tests -- --test-case='write the <name> save seed' --no-skip` for `fight`, `world`, `delta` and `scratch`. Then confirm `"the save fuzz target's seeds load in its game"` passes: `pixi run project_n test --suite game_tests --test save_exactness`.

- [ ] **Step 7: Update the docs** listed under Files (src/AGENTS.md, design doc 0002).

- [ ] **Step 8: Task gate**

Run: `pixi run project_n test` and `pixi run project_n lint --range <task base>..HEAD`
Expected: all pass, no findings.

- [ ] **Step 9: Commit**

```bash
git add src/game/npcs.hpp src/game/npcs.cpp src/game/host.cpp tests/unit/game tests/fuzz/corpus/save src/AGENTS.md docs/design/0002-engine-architecture.md
git commit -m "fix(game): keep each NPC's relationships to the ones it met last"
```

---

### Task 2: Measure saves over hours of play again, and record the Outcome

`review: light` · `implementer: sonnet` · main checkout, runs alone

**Files:**
- Modify: `docs/superpowers/specs/2026-11-19-relationships-bounded-design.md` (an `## Outcome` section; Status becomes "implemented")
- Modify: `docs/superpowers/specs/2026-11-16-phase6-chunk5c1-world-kept-delta-saves-design.md` (one line under its "Saves over hours of play" findings, pointing to this spec's Outcome; the measured tables stay as they are)

**Interfaces:**
- Consumes: Task 1's bound. The skipped case `measure the shipped world's saves over hours of play` (`tests/unit/game/shipped_world_tests.cpp`) is unchanged.

- [ ] **Step 1: Run the measurement** (release, as the case's comment says; it ran ~8 minutes before the bound, and should be faster now):

Run: `pixi run project_n test --mode release --target game_tests -- '--test-case=measure the shipped world?s saves over hours of play' --no-skip`
Expected: five checkpoints logged, each load's hashes matching (asserted). The `relationships` count stays at most 500,000 (50,000 NPCs × 10, as nobody is met) from 10 h on, the relationships' bytes at most 14,000,008, and the month step roughly flat from 10 h to 250 h. If the count passes 500,000 or the step still grows with play time, stop and report.

- [ ] **Step 2: Write the Outcome**: the run's three tables in the 5c1 spec's layout (sizes, parts of the raw save, counts), set beside 5c1's 250 h figures (755 MB raw, 64.8 MB on disk, 37.4M relationships, month step 65.6 ms), and how many months the NPCs took to fill. Note that times are good to about 50% between runs (5c1's finding) and sizes are exact. Mark the spec implemented, and add the pointer line to the 5c1 spec.

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-11-19-relationships-bounded-design.md docs/superpowers/specs/2026-11-16-phase6-chunk5c1-world-kept-delta-saves-design.md
git commit -m "docs(design): record how bounded relationships measured"
```
