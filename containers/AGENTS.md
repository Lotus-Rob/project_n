# containers/: artefact images

## Scope
`linux.Containerfile` (Linux artefacts, container checks), `wine.Containerfile` (linux + Wine; open source, so CI caches it in GHCR), `msvc.Containerfile` (windows-msvc via msvc-wine, layered on wine; never pushed), `windows/buildtools.vsconfig` (native Windows MSVC).

## Rules
- Images are tagged by a hash of their inputs (`IMAGES`/`IMAGE_INPUTS` in tools/project_n.py); `project_n` builds them on first use. Add any new file an image copies to `IMAGE_INPUTS` and `.containerignore`.
- Toolchains come from `pixi.lock`, never from apt. apt is only for download tools, GNU coreutils and Wine, all from the pinned snapshot.
- Everything is pinned: base image digest, apt snapshot date, Pixi release + checksum, msvc-wine commit, MSVC and SDK versions.
- The MSVC image is local-only: its licence forbids redistribution. Never push it; building it needs `PROJECT_N_ACCEPT_MSVC_LICENSE=1`.
- `buildtools.vsconfig` and `VS_TOOLSET` in tools/project_n.py name the same MSVC version as the msvc image.

## Commands
- `pixi run project_n image` builds missing images · `pixi run project_n shell --preset <p>` debugs inside one
- `pixi run project_n artifact --preset <p>` builds into `dist/<preset>/` with `manifest.json`

## Skills
- bump-toolchain: every pin location and the update order

## Gotchas
- Ubuntu 25.10+ ships uutils coreutils; its `ln -sf`/`install` break libtool installs ("Already exists"). The image swaps in `coreutils-from-gnu`.
- Wine writes its registry only when `wineserver` exits: a prefix created in a `RUN` step lacks `HKCU\Environment` (no `%TEMP%`), and `cl.exe` fails with D8037. The msvc image recreates it with `wineboot -i && wineserver -w`.
- MSVC 14.50 ignores `/std:c++23` (warning D9002, compiles as C++14). xmake.lua uses `/std:c++23preview`; src/core/cxx23.hpp catches any fallback.
- Podman on Windows: bind-mounting an NTFS checkout into WSL is slow; keep a clone inside WSL for container builds.

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|
