# containers/: artefact images

## Scope
`linux.Containerfile` (Linux artefacts, container checks), `msvc.Containerfile` (windows-msvc: linux + Wine + msvc-wine, with MSVC mounted at `/opt/msvc`), `windows/buildtools.vsconfig` (native Windows MSVC).

## Rules
- Images are tagged by a hash of their inputs (`IMAGES`/`IMAGE_INPUTS` in tools/project_n.py); `project_n` builds them on first use. Add any new file an image copies to `IMAGE_INPUTS` and `containers/.containerignore`.
- Toolchains come from `pixi.lock`, never from apt. apt is only for download tools, GNU coreutils and Wine, all from the pinned snapshot.
- Everything is pinned: base image digest, apt snapshot date, Pixi release + checksum, msvc-wine commit, MSVC and SDK versions.
- MSVC never goes into an image (its licence forbids redistribution): `project_n image` installs it into `~/.xmake/project_n-tools/msvc-<hash of msvc.Containerfile>` (needs `PROJECT_N_ACCEPT_MSVC_LICENSE=1`), mounted at `/opt/msvc`. That keeps every image pushable.
- `buildtools.vsconfig` and `VS_TOOLSET` in tools/project_n.py name the same MSVC version as `MSVC_VERSION` in msvc.Containerfile.

## Commands
- `pixi run project_n image` builds missing images · `pixi run project_n shell --preset <p>` debugs inside one
- `pixi run project_n artifact --preset <p>` builds into `dist/<preset>/` with `manifest.json` (macOS: `project_n.app`), symbols in `dist/<preset>-symbols/`, and a zip of each

## Skills
- bump-toolchain: every pin location and the update order

## Gotchas
- Ubuntu 25.10+ ships uutils coreutils; its `ln -sf`/`install` break libtool installs ("Already exists"). The image swaps in `coreutils-from-gnu`.
- Wine writes its registry only when `wineserver` exits: a prefix created in a `RUN` step lacks `HKCU\Environment` (no `%TEMP%`), and `cl.exe` fails with D8037. The msvc image recreates it with `wineboot -i && wineserver -w`.
- msvc-wine's install includes an empty servicing stub of Microsoft's newest toolset (`VC/Tools/MSVC/14.51.*`), which XMake took for the toolset. `ensure_msvc` deletes toolset folders without `bin/`.
- MSVC 14.50 ignores `/std:c++23` (warning D9002, compiles as C++14). xmake.lua uses `/std:c++23preview`; src/core/cxx23.hpp catches any fallback.
- Podman on Windows: bind-mounting an NTFS checkout into WSL is slow; keep a clone inside WSL for container builds.

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|
