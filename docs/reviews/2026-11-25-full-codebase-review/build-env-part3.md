# Review: build-env (part 3)

### BUILD-ENV-21: Loosely pinned clang compiler package (==22.1) breaks the exact-pin rule

- **Severity:** info · **Verdict:** confirmed · **Category:** pin-hygiene · **Status:** new · **Reviewer confidence:** low
- **Where:** `pixi.toml:44`

`clangxx_linux-64 = "==22.1"` is major.minor while every other tool is exact. The comment on macOS says Linux uses major.minor. AGENTS.md says pins are exact.

**Impact:** The resolved patch version depends on pixi.lock only. A `pixi update`, or a lock regeneration, could change the compiler patch version without any pixi.toml diff.

**Recommendation:** Document the exception in AGENTS.md, or pin to the full version if conda-forge's build string allows.

**Verification:** pixi.toml:44 has clangxx_linux-64 = "==22.1" while the other LLVM pins are ==22.1.8. The macOS line 68 comment says 'Linux's is major.minor', so the difference is known. AGENTS.md says pins are exact, and this exception is not listed there. The lock file fixes the actual patch version.

### BUILD-ENV-22: LICENSE is AGPL-3.0 while binaries link zlib/MIT/Apache/OFL/FTL pieces: compatibility reasoning is not documented

- **Severity:** info · **Verdict:** unverifiable · **Category:** licence · **Status:** new · **Reviewer confidence:** low
- **Where:** `LICENSE:1`

The project is AGPL-3.0-or-later. Third-party licences are all permissive or compatible with GPLv3 (FTL is compatible with GPLv3 only, not v2). The notices file states the choices, but nowhere records why each combination is compatible. The font OFL and art assets have separate terms. The LICENSE text carries no per-file SPDX headers (not checked here).

**Impact:** Store distribution (Steam, console) conflicts with AGPL for any non-owner redistributor, and contributors' copyright is not assigned. This is a business risk for the project's goals rather than a defect.

**Recommendation:** Add a short licence-compatibility note to docs/design, and decide on a CLA or DCO policy before outside contributions arrive.

**Verification:** This is a licensing and business opinion. The facts (AGPL-3.0-or-later with THIRD_PARTY_NOTICES.md) are consistent with the design doc, which lists AGPL with third-party notices. Whether a compatibility note or CLA is wanted is the owner's call, and a reading of the repo cannot decide it.

### BUILD-ENV-23: VS Code settings require a hand edit and skip-worktree on Windows

- **Severity:** info · **Verdict:** unverifiable · **Category:** dx · **Status:** new · **Reviewer confidence:** low
- **Where:** `.vscode/settings.json:10`

`clangd.path` points at `.pixi/envs/default/bin/clangd`, valid only on Linux and macOS. The comment tells Windows developers to edit the line and run `git update-index --skip-worktree`. `extensions.json` recommends clangd only and does not warn about ms-vscode.cpptools, which conflicts with clangd's IntelliSense.

**Impact:** Windows contributors get a broken editor setup or an accidental commit of the local edit. A bad rebase can fail on a skip-worktree file.

**Recommendation:** Use `${workspaceFolder}/.pixi/envs/default/Library/bin/clangd.exe` via a platform-specific `settings` key (`clangd.path` per OS) or a wrapper script in tools/, and add `unwantedRecommendations` for cpptools.

**Verification:** This is a developer-experience suggestion. The doc-recorded design (0001 §4, Q6) commits the VS Code clangd path. The Windows workaround is documented in the settings comment, as the finding says. How Windows contributors fare cannot be decided by reading.

### BUILD-ENV-24: Container user is hard-coded to uid 1000

- **Severity:** info (reviewer said low) · **Verdict:** refuted · **Category:** container · **Status:** new · **Reviewer confidence:** low
- **Where:** `containers/linux.Containerfile:23`

The image renames Ubuntu's uid-1000 user to `dev`, owns `/src` and `/home/dev/.xmake` by it, and runs with `--userns=keep-id`. Pixi envs are in `/home/dev/.pixi-envs`. A host user with another uid maps to a uid that doesn't own `/home/dev/.xmake` or `/home/dev/.pixi-envs` (read-only for others).

**Impact:** `check --container` or `artifact` fails for contributors whose host uid is not 1000, or on CI runners with a different uid. The failure would be a confusing permission error.

**Recommendation:** Make `/home/dev` and its subdirectories world-writable or use a build ARG for the uid, or document the uid assumption in containers/AGENTS.md.

**Verification:** tools/project_n.py:661 runs podman with `--userns=keep-id:uid=1000,gid=1000`. The host user is therefore mapped to uid 1000 whatever its real uid, and that uid owns /home/dev. The Containerfile comment says the same. The scenario of a mismatched uid does not occur.
