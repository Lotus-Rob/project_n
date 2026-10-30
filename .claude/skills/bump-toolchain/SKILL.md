---
name: bump-toolchain
description: Change a pinned tool or toolchain version in project_n. Use when bumping, upgrading, downgrading or pinning gcc, clang/LLVM, lldb, gdb, xmake, python, cmake or any other pixi.toml package, the sysroot/glibc baseline, the Ubuntu base image or apt snapshot, the Pixi release in the image, msvc-wine, the MSVC toolset or the Windows SDK.
---

# Bumping a pinned toolchain

Owned by: containers/AGENTS.md. Rationale: docs/design/0001 §6.1 (pinning policy), §7.1.

## Where each pin lives

| What | Pin locations (change together) |
|---|---|
| Any Pixi package | `==X.Y.Z` in `pixi.toml`, then `pixi.lock` |
| LLVM | Every LLVM package in `pixi.toml` at one version: `clang-format`, `clang-tools`, `clangxx_linux-64` (major.minor only), `lld`, `libcxx`, `libcxx-devel`, `lldb`. lldb's libc++ formatters only match their own release, so wait until conda-forge ships all of them |
| GCC | `gxx_linux-64` in `pixi.toml`; the libstdc++ pretty-printers follow automatically (tools/debug/gdbinit) |
| glibc baseline | `sysroot_linux-64`. Changing it changes which distros can run the game: ask the user first and update design doc §5.2 |
| XMake | `xmake` in `pixi.toml` and `set_xmakever()` in `xmake.lua` |
| Ubuntu base | The digest in `containers/linux.Containerfile` `FROM` |
| apt snapshot | `APT_SNAPSHOT` in `linux.Containerfile` and `msvc.Containerfile` (keep them equal) |
| XMake on macOS | `XMAKE_MACOS_SHA256` in tools/project_n.py: the checksum of the upstream `xmake-bundle-v<version>.macos.arm64`, used because conda-forge has no osx-arm64 XMake |
| Pixi in the image | `PIXI_VERSION` and `PIXI_SHA256` in `containers/linux.Containerfile`; raise `requires-pixi` in `pixi.toml` only if the project needs a newer Pixi feature |
| msvc-wine | `MSVC_WINE_COMMIT` in `containers/msvc.Containerfile` |
| MSVC / Windows SDK | `MSVC_VERSION` and `SDK_VERSION` (`ENV`) in `containers/msvc.Containerfile` (a change gives a new MSVC install directory), the matching components in `containers/windows/buildtools.vsconfig`, and `VS_TOOLSET` in tools/project_n.py: all three name the same toolset |

## Steps

1. One layer per commit: a compiler bump and an image bump are separate commits, so a regression bisects to one change.
2. Edit the pin(s) from the table. For Pixi packages, run `pixi install` to update `pixi.lock`. Never edit the lock by hand.
3. `pixi run project_n check`. A new Pixi environment gets its own XMake package directory automatically (tools/env/activate.sh), so packages rebuild with the new compiler on the first build.
4. `pixi run project_n check --container`: the image tag is a hash of its inputs, so changed pins rebuild the image. For MSVC changes this needs `PROJECT_N_ACCEPT_MSVC_LICENSE=1`.
5. Compiler, runtime or sysroot changes also need `pixi run project_n check --sanitizers` and `pixi run project_n artifact --preset <p>` for each affected preset (artifact checks the binary's runtime dependencies).
6. An LLVM bump can add clang-tidy checks or findings: fix the code; never disable the new checks in `.clang-tidy`, and suppress a single finding only as the last resort (root AGENTS.md).
7. An XMake bump: check whether the overrides in `xmake/repo/` are still needed, and whether `xmake-requires.lock` changed.
8. Update the versions quoted in design doc §5 and in README's install table if they changed, then commit with the lock: `build(env): bump <tool> to X.Y.Z` (Pixi) or `build(containers): ...` (images, msvc-wine, `.vsconfig`).
9. `pixi run project_n clean --packages` frees the old package directories once the bump is committed.

## Gotchas
- `check --container` covers MSVC under msvc-wine only. After an MSVC or SDK bump, tell the user the native Windows build still needs verifying.
