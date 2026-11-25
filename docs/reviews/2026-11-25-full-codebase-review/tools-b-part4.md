# Review: tools-b (part 4)

### TOOLS-B-31: prune-ci-caches looks at only the newest 1000 entries

- **Severity:** info · **Verdict:** confirmed · **Category:** robustness · **Status:** new · **Reviewer confidence:** low
- **Where:** `tools/project_n.py:2045`

`gh cache list --limit 1000` caps the listing. Entries past the first 1000 for a ref (many families times versions times jobs) are never seen, so older superseded entries accumulate unpruned.

**Evidence:** line 2045

**Impact:** Low. It only matters if a ref has more than 1000 cache entries.

**Recommendation:** Paginate, or warn when the limit is hit.

**Verification:** Line 2045 does pass --limit 1000. The pruning runs per ref in CI, and more than 1000 entries on one ref is unlikely. Negligible.
