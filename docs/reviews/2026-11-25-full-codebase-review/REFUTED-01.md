# Raised but refuted on verification (1)

Kept for the record; each unit file holds the refutation reason.

| ID | Severity | Verdict | Status | Where | Finding |
|---|---|---|---|---|---|
| ASSETS-DATA-13 | info | refuted | new | `assets/data/strings.yaml:6` | tablet_caption has the same Chinese text in English and Chinese |
| TOOLS-A-27 | low | refuted | new | `tools/project_n.py:224` | Lock-file restore leaves dropped entries dropped when anything was also added |
| BUILD-ENV-24 | info | refuted | new | `containers/linux.Containerfile:23` | Container user is hard-coded to uid 1000 |
| CI-14 | info | refuted | new | `.github/workflows/ci.yml:47` | `main` ref hard-coded, and a commit-range edge cases in lint |
| X-RESILIENCE-15 | low | refuted | new | `src/app/view.hpp:47` | Frame-time extremes: only labels and the tick catch-up are bounded; camera, damage numbers, cursor and animations take raw wall deltas |
| X-RESILIENCE-16 | info | refuted | new | `src/app/app.hpp:306` | Logic-thread failure is signalled by an unsynchronised-looking flag and the main loop never stops the logic thread promptly |
