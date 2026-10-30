# 0001: Repository and build environment

| | |
|---|---|
| Status | **Accepted**: phases 1–4 implemented; S3 (Podman on Windows) and cross-machine S2 open |
| Author | Rob G |
| Created | 2026-10-29 |
| Revised | 2026-10-30: phase 4: GitHub Actions CI on Linux, Windows and macOS runners (§12); `macos-clang` implemented (§5.7, S7 passed); S5 passed in CI. Earlier the same day: macOS planned as a native `macos-clang` preset (D13). 2026-10-29: development runs natively in a portable Pixi environment; containers only produce artefacts; headers only (no C++ modules); Conventional Commits + git-cliff + SemVer; open questions Q1–Q10 resolved (Appendix B); knowledge split across AGENTS.md, skills and design docs (§10); Steam Runtime dropped, so Linux artefacts are built in an Ubuntu + Pixi image with the glibc baseline set by the conda-forge sysroot |
| Scope | Repository layout, development environment, artefact containers, toolchain pinning, testing, build tooling, commits and versioning, agent knowledge (AGENTS.md and skills) |

---

## 1. Goals and non-goals

### Goals

1. **Portable, replicable development environment.** The repository declares its whole native toolchain: compilers, XMake, debuggers, formatters and Python. A single command installs it into a project-local environment, the same way a virtual environment works, on Linux and Windows. You develop by opening the folder in an editor on the host. There are no containers in the development loop.
2. **Reproducible build artefacts.** Containers have one job: producing release artefacts that are identical for the same commit, whichever machine builds them.
3. **The same compilers everywhere.** The development environment and the artefact containers install from **one lock file**.
4. **Portable Linux binaries.** Linux artefacts run on any distro with glibc at or above a pinned baseline. The conda-forge sysroot sets that baseline, and the C++ runtime is linked statically.
5. **Three compilers:** GCC, Clang (with libc++) and MSVC, all building C++23 through XMake. The project uses conventional headers, not C++ modules.
6. **One command surface** for humans, CI and agents.
7. **Open-source tooling first:** Pixi/conda-forge, Podman, XMake, LLVM, GCC, Wine, Python and git-cliff. Proprietary pieces (MSVC, the Windows SDK) are isolated and pinned.
8. **Test-suite ready.** doctest unit tests run on every preset, and the layout scales to a large suite.
9. **Machine-readable history.** Conventional Commits from the first commit, so changelogs and SemVer versions are generated, not written by hand.
10. **Agent-maintainable.** Agents get short, always-loaded facts from AGENTS.md and load procedures as skills on demand. Both are kept current as part of normal work.

### Non-goals (for this document)

- Engine and game architecture, and the choice of graphics, audio and windowing libraries. This includes which Linux libraries are bundled and which are expected from the system (O2).
- Store integration, packaging and depots.
- Console platforms.
- CI workflows (deferred, §12).

---

## 2. Decisions at a glance

| # | Decision | Rationale |
|---|---|---|
| D1 | **Pixi** ([pixi.sh](https://pixi.sh)) defines the development environment in `pixi.toml` with a committed `pixi.lock`. The environment lives in `.pixi/` inside the project. | It works like a venv, but for native tools. conda-forge ships GCC, Clang, lld, libc++, clang-format, clang-tidy, clangd, gdb, lldb, XMake, Python and git-cliff. One lock file covers Linux and Windows. conda-forge usually trails upstream compiler releases by weeks to months, which is accepted. |
| D2 | **Podman (rootless)** is the container engine, used **only for artefact builds** and container-based checks. Containerfiles follow the OCI format. | Open source and daemonless. Runs on Windows through `podman machine` (WSL2). |
| D3 | **Linux artefacts are built in `project_n-linux`**: Ubuntu LTS pinned by digest, plus `pixi install --locked`. The image has no distro compilers. The glibc baseline comes from the conda-forge sysroot pinned in `pixi.toml`. The C++ runtime is linked statically (`-static-libstdc++` for GCC, static libc++ for Clang). | Artefacts use exactly the development compilers. The container's job is isolation, not providing tools. Ubuntu is the most common base image. |
| D4 | **All images use Ubuntu LTS**, pinned by digest, with apt pinned to a snapshot date for the few distro packages (Wine). | One base to pin and update. |
| D5 | **MSVC artefacts are built under Wine** ([msvc-wine](https://github.com/mstorsjo/msvc-wine)) in an image layered on `project_n-linux`. MSVC itself is installed outside the image and mounted in. | Podman can't run Windows containers, and conda-forge can't ship MSVC. The MSVC licence forbids redistribution, so no image contains it and images can be cached in a registry. |
| D6 | **On native Windows, MSVC comes from pinned Build Tools**: a versioned `.vsconfig` component, selected through XMake's `--vs_toolset`. Everything else comes from the Pixi environment. | MSVC is the one tool Pixi can't provide, so it is pinned separately. |
| D7 | **XMake** owns the build graph, dependencies (`xmake-repo` packages with a committed lock file) and tests (`add_tests`, `xmake test`). | Avoids duplicating XMake features in scripts. |
| D8 | **`tools/project_n.py` is the one entry point.** It is stdlib-only Python, exposed as the Pixi task `project_n`, so you run `pixi run project_n <command>`. | It hides Podman, the per-preset XMake config and the release steps. `pixi run` guarantees the pinned tools are on PATH. |
| D9 | **Native is the default for every command except `artifact`.** `windows-msvc` falls back to its container on Linux hosts. | Development and testing happen in the Pixi environment, and containers are used only when needed. |
| D10 | **Knowledge has three tiers.** AGENTS.md holds short facts and pointers and is always loaded. Skills hold procedures and detail and are loaded on demand. Design docs hold rationale. Each fact lives in exactly one tier (§10). | Keeps agent context small and procedures complete, with no duplicated text that can drift. |
| D11 | **Conventional Commits 1.0.0**, checked by `project_n commit-check` through a committed `commit-msg` hook. **Linear history**: rebase only, no merge commits. | Rules and enforcement from day one, with no extra dependency. |
| D12 | **git-cliff** generates `CHANGELOG.md` and the next **SemVer** version from commits. `project_n release` applies both atomically. | One command per release, and the version is always derived from history. |
| D13 | **macOS is a native-only preset, `macos-clang`** (Apple Silicon), built in the Pixi environment on a Mac or a CI macOS runner. No cross-compiling, VM or compatibility layer on Linux. | osxcross needs the Xcode SDK and a macOS VM needs macOS itself; both licences allow use only on Apple hardware. Neither could run the game either: no Metal outside real Macs. |

---

## 3. Build matrix

A **preset** is the combination of target platform and toolchain. The build mode (`debug`, `release`, `releasedbg`) is an orthogonal flag.

| Preset | Target | Compiler source | Development (native) | Artefact / container check | Tests run in |
|---|---|---|---|---|---|
| `linux-gcc` | Linux x86_64 | conda-forge GCC (`pixi.lock`) | Pixi env on host | `project_n-linux` | Pixi env; Linux image |
| `linux-clang` | Linux x86_64 | conda-forge Clang + libc++ (`pixi.lock`) | Pixi env on host | `project_n-linux` | Pixi env; Linux image |
| `windows-msvc` | Windows x64 | MSVC 14.50.35717 (VS 2026, msvc-wine `--msvc-version 18.0`) + Windows SDK 10.0.26100: `.vsconfig` natively, msvc-wine in the container | Windows host: Pixi env + Build Tools | `project_n-msvc` | Wine in container; native Windows is authoritative |

| `macos-clang` (§5.7) | macOS arm64 | conda-forge Clang + libc++ (`pixi.lock`), Apple SDK from the Xcode Command Line Tools | Mac: Pixi env | none: `artifact` runs natively on a Mac or CI runner | Pixi env on macOS |

Possible later additions, not part of the initial setup:

- `windows-clang-cl`. Clang is in the Pixi environment on Windows, so this preset costs little.
- Sanitizer modes for `linux-clang`.

**Pins confirmed by S4** (Linux, 2026-10-29). Phase 2 commits them to `pixi.toml`. Every direct dependency is pinned with `==` (§6.1). The Clang activation package is versioned by major.minor only (`==22.1`).

| Package | Version | Notes |
|---|---|---|
| `gxx_linux-64` (GCC) | 16.2 | Linux only |
| `clangxx_linux-64`, `lld`, `libcxx`, `libcxx-devel`, `clang-format`, `clang-tools`, `lldb` | 22.1.8 | One version for all LLVM packages (§7.1 rule 9). Moves to 23 once conda-forge ships lldb 23. `libcxx` is used on Linux only |
| `sysroot_linux-64` | 2.28 | glibc baseline. S4 binaries needed at most glibc 2.27 |
| `xmake` | 3.1.2 | |
| `gdb` | 18.1 | Linux only |
| `git-cliff` | 2.14.2 | |
| `python` (+ `pip`, `setuptools`, `wheel`) | 3.14.x | XMake only accepts a build-tool Python that has all three |
| `wayland` | 1.26 | Provides only the build-machine `wayland-scanner` (§7.1) |
| `patchelf` | latest | Used by `artifact` to strip the rpath that conda's GCC hard-codes |
| `doctest` / `libsdl3` | 2.5.3 / 3.4.16 | Come from `xmake-repo`, not Pixi. Listed for reference |

MSVC 14.50.35717 and Windows SDK 10.0.26100 were confirmed in S1 (container). S5 confirms the native Windows `.vsconfig` components match.

---

## 4. Repository layout

```
project_n/
├── AGENTS.md                 # always-loaded: map, commands, brief rules, skill index (§10.3)
├── CLAUDE.md                 # "@AGENTS.md"
├── README.md                 # human quick start: install pixi, `pixi run project_n doctor`
├── CHANGELOG.md              # generated by git-cliff at release time; never hand-edited
├── cliff.toml                # git-cliff: changelog template + SemVer bump rules
├── pixi.toml / pixi.lock     # dev environment; lock committed (linux-64, win-64)
├── xmake.lua                 # C++23, warnings, presets → toolchains, set_version()
├── xmake-requires.lock       # committed package lock
├── xmake/repo/               # local xmake package overrides (§7.1 rule 7)
├── .clang-format  .clang-tidy  .editorconfig  .gitattributes  .gitignore
│                             # .gitignore: .pixi/, build/, dist/, __pycache__/
├── .githooks/
│   └── commit-msg            # POSIX sh: `pixi run project_n commit-check "$1"`
│
├── .claude/skills/           # on-demand procedures (§10.4)
│   ├── git-commit/SKILL.md
│   └── agent-docs/SKILL.md
│
├── tools/
│   ├── AGENTS.md
│   ├── project_n.py          # the single entry point (§8)
│   ├── test_project_n.py     # unittest self-checks
│   ├── debug/gdbinit         # pretty-printer registration (§7.1 rule 8)
│   └── env/activate.sh       # Pixi activation: package dir per lock, gdbinit hook (§7.1 rules 8, 11)
│
├── containers/
│   ├── AGENTS.md
│   ├── linux.Containerfile   # Ubuntu LTS (digest) + pixi + `pixi install --locked`
│   ├── msvc.Containerfile    # FROM project_n-linux + Wine + msvc-wine; MSVC mounted at /opt/msvc
│   └── windows/
│       └── buildtools.vsconfig
│
├── src/                      # structure decided by the architecture doc
│   └── AGENTS.md
├── tests/
│   ├── AGENTS.md
│   └── xmake.lua
│
└── docs/
    └── design/               # numbered design docs (rationale tier)
```

Other notes:

- Editor configuration is not committed.
- Nested AGENTS.md files exist only where an area has rules that differ from its parent's.
- Skills beyond the initial two are added as described in §10.5.

---

## 5. Environments

### 5.0 Installing host prerequisites

Every build tool comes from Pixi, so the host needs only the tools below. Install each one **when the phase that needs it starts**, not all up front.

- **README.md carries this table for humans** (since phase 2), and `doctor` (`INSTALL_HINTS` in `tools/project_n.py`) checks each tool and prints the install command for the current OS. Keep the three in sync.
- Agents never install system packages themselves. They run `doctor` and give the user its instructions.

| Tool | Needed from | Linux (CachyOS / Arch) | Windows | Dev machine (2026-10-29) |
|---|---|---|---|---|
| Git | phase 0 | `sudo pacman -S git` | `winget install Git.Git` | 2.56 ✓ |
| **Pixi** | phase 1 (S4) | `sudo pacman -S pixi` (in `extra`). Elsewhere: `curl -fsSL https://pixi.sh/install.sh \| sh`, then add `~/.pixi/bin` to PATH (fish: `fish_add_path ~/.pixi/bin`) | `winget install prefix-dev.pixi` | **missing** |
| Podman | phase 1 (S1, S6) | `sudo pacman -S podman`. Rootless mode needs subordinate IDs; if `podman info` reports an error, run `sudo usermod --add-subuids 100000-165535 --add-subgids 100000-165535 $USER` | `winget install RedHat.Podman`, then `podman machine init` and `podman machine start` | 6.1.4, rootless ✓ |
| Xcode Command Line Tools | phase 4, macOS only | n/a | n/a (`xcode-select --install` on the Mac) | n/a |
| VS Build Tools (MSVC) | phase 1 (S5), Windows only | n/a | Run the Build Tools bootstrapper (from visualstudio.microsoft.com → Downloads → Build Tools) with `--config containers\windows\buildtools.vsconfig --passive --wait`. The winget ID and exact command are confirmed in S5 and then written here | n/a |

Everything else comes from `pixi install`: compilers, XMake, debuggers, clang tools, Python and git-cliff. This includes the msvc-wine toolchain, which is downloaded inside the container. Host copies of GCC, Clang, XMake, gdb or Wine can stay installed. They're ignored, because `pixi run` puts the environment first on PATH.

### 5.1 Development environment (Pixi, on the host)

**Host prerequisites:** see §5.0.

**Setup:**

1. `pixi install` creates `.pixi/envs/default` from `pixi.lock`.
2. `git config core.hooksPath .githooks` enables the commit hook. `doctor` reminds you if it's missing.

**Daily loop:**

1. Open the folder in your editor. Point its clangd, clang-format and debugger at `.pixi/envs/default/bin/`, or start the editor from `pixi shell`.
2. `pixi run project_n build --preset linux-clang` builds into `build/<preset>/<mode>/` and writes `build/compile_commands.json`, which `.clangd` reads.
3. Run or debug the game from its stable path, `build/<preset>/<mode>/bin/<target>`, or use `pixi run project_n run --preset linux-clang --debug`.
4. `pixi run project_n test --preset linux-gcc` runs the tests.
5. `pixi run project_n check` runs the merge gate (§8).

**Notes:**

- conda-forge's Linux compilers build against their own glibc sysroot, not the host's headers, so the host distro doesn't leak into builds. Spike S4 confirms that XMake picks up these triplet-prefixed compilers.
- The game runs directly on the host and uses the host's GPU drivers through the standard Vulkan and GL loaders.

### 5.2 `project_n-linux` image (Linux artefacts and container checks)

- **Base:** Ubuntu 26.04 LTS, **pinned by digest**, with apt pinned to a snapshot date (`apt-get update --snapshot`). It provides only a shell, libc, download tools and Pixi (exact release, checksum-verified).
- **GNU coreutils:** Ubuntu 25.10+ defaults to uutils coreutils, whose `ln -sf` and `install` break libtool installs ("Already exists"; libX11 failed in S6). The image swaps in GNU coreutils with Ubuntu's own `coreutils-from-gnu` package.
- **Build context:** `.containerignore` admits only the files the image copies (`pixi.toml`, `pixi.lock`, `tools/env/activate.sh`), so the multi-GB `.pixi/` and `build/` never reach Podman.
- **Build:** `pixi install --locked` into a detached environment path outside `/src`, so the bind-mounted repository's `.pixi/` doesn't collide with the host's.
- **Linking:**
  - The glibc baseline is **2.28**, from the conda-forge sysroot, not from Ubuntu's glibc. It covers distros from about 2019 onward: RHEL 8, Ubuntu 20.04, Debian 10.
  - The C++ runtime is linked statically.
  - Nothing may be linked from the Pixi environment's shared libraries. Anything else the binary needs at runtime is either a common system library or ships with the game (O2).
  - `artifact` fails if `ldd` reports a library that resolves into the Pixi environment.
- **User:** a non-root `dev` user, run with `--userns=keep-id`.
- **Tag:** `project_n-linux:<hash of Containerfile + pixi.lock>`.

### 5.3 `project_n-msvc` image (Windows artefacts on Linux)

- **Base:** `FROM project_n-linux`, which provides the Pixi environment with XMake and Python (`vsdownload.py` runs with Pixi's Python).
- **Contents:** Wine 10.0, winbind and msitools from the same apt snapshot, and msvc-wine pinned by commit. No Microsoft files.
- **MSVC:** `project_n image` installs it once with `vsdownload.py --msvc-version 18.0 --sdk-version 10.0.26100 --architecture x64` plus msvc-wine's `install.sh`, run in the image, into `~/.xmake/project_n-tools/msvc-<hash of msvc.Containerfile>` on the host. Every windows-msvc container mounts that directory at `/opt/msvc`. Until phase 4 MSVC lived inside the image, which therefore couldn't be cached in a registry and was rebuilt (unpacking 3.9 GB) on every CI run.
- **XMake:** `xmake f -p windows -a x64 --toolchain=msvc --sdk=/opt/msvc`. XMake's msvc toolchain supports msvc-wine directly, so no custom toolchain is needed.
- **Wine prefix:** recreated as the image's last step with `wineboot -i`, then `wineserver -w`. Wine writes its registry only when `wineserver` exits, and an earlier `RUN` step that ran Wine ended first. That left a prefix without `HKCU\Environment`, so `%TEMP%` was undefined and every `cl.exe` call failed with D8037 "cannot create temporary il file" (S1). Ubuntu keeps `wineserver` in `/usr/lib/x86_64-linux-gnu/wine`, which the image adds to `PATH`.
- **Licence:** installing MSVC requires accepting its licence (`PROJECT_N_ACCEPT_MSVC_LICENSE=1`), and it never enters an image, so the image can be pushed (a unit test checks that no pushed image runs `vsdownload.py`). The Build Tools licence terms have been confirmed to fit the project, for both the container and native installs.
- **Tests:** Windows test binaries run under Wine.

### 5.4 Native Windows

- **Toolchain:** `vs_BuildTools.exe --config containers/windows/buildtools.vsconfig`, which installs a **versioned** MSVC component (for example `Microsoft.VisualStudio.Component.VC.14.50.*`).
- **Pinning:** XMake uses `--vs_toolset=<pinned>`.
- **Environment:** `pixi install` provides XMake, Python, the Clang tools and git-cliff from the `win-64` section of the same lock file.
- **Debugging:** native, using Visual Studio, RemedyBG, WinDbg or VS Code with cppvsdbg.

### 5.5 Windows host running containers

- Podman Desktop or `podman machine` with the WSL2 backend. The same commands work from PowerShell.
- **Gotcha (documented in `containers/AGENTS.md`):** bind-mounting an NTFS checkout into WSL is slow, so keep a clone inside the WSL filesystem for heavy container builds. `doctor` warns when it detects this.

### 5.6 Why not Windows containers?

- Podman has no Windows-container support.
- The alternatives (Docker on Windows Server, or containerd with nerdctl) add a second engine and multi-GB images, and they need Windows CI hosts.
- msvc-wine plus a pinned native toolset covers the same need.
- If S1 fails, the fallback is containerd with nerdctl on Windows, which is open source.

### 5.7 Native macOS

Implemented 2026-10-30 and verified on GitHub's `macos-26` (Apple Silicon) runners (S7).

- **Target:** Apple Silicon (`osx-arm64`) only. Intel Macs stop getting new macOS releases, so no universal binaries.
- **Toolchain:** the same LLVM 22.1.8 packages as Linux from the `osx-arm64` section of `pixi.lock` (`clangxx_osx-arm64` carries the full version there, Linux's only major.minor). The `pixi-clang` XMake toolchain picks the `arm64-apple-darwin20.0.0-` prefix on macOS hosts. lldb is the debugger; gdb doesn't support Apple Silicon.
- **SDK:** from the Xcode Command Line Tools (`xcode-select --install`, §5.0). conda's `sdkroot_env` package sets `SDKROOT` through `xcrun`. The SDK can't be pinned like a package, so `doctor` reports its version. `MACOSX_DEPLOYMENT_TARGET=13.0` (Pixi activation) sets the oldest supported macOS for clang and CMake alike.
- **XMake:** conda-forge has no `osx-arm64` build, so `project_n` downloads the upstream `xmake-bundle-v<version>.macos.arm64` for `set_xmakever()`'s version, checked against `XMAKE_MACOS_SHA256`, into `~/.xmake/project_n-tools/`. Adding `osx-arm64` to the conda-forge xmake feedstock would let the lock cover it again.
- **C++ runtime:** conda's libc++ as a **dylib**, shipped next to the binary (`artifact` copies `libc++.1.dylib`, removes the rpath into `.pixi` and re-signs, since arm64 refuses binaries whose ad-hoc signature broke). Linked statically, as on Linux, it aborted every program at exit: libSystem loads the system libc++ anyway (through libobjc), and the two copies freed each other's memory. libc++abi is the system's, as for conda's own libc++. Only libc++ is linked from the env, by path: a `-L` into it let conda's libiconv shadow the SDK's.
- **Availability markers:** libc++ marks features by the macOS release whose *system* libc++ has them (`std::to_chars` needs 13.3). We always run conda's libc++, so `_LIBCPP_DISABLE_AVAILABILITY` turns the markers off.
- **Location rule:** `macos-clang` builds natively only on a macOS host; on Linux and Windows hosts, `project_n` refuses it with a pointer to CI, rather than falling back to a container. `check` and `build --preset all` skip it there and say so, so the gate stays runnable on every host; CI's macOS runner covers it.
- **Signing and notarisation:** later, with packaging. `rcodesign` (open source) can sign and notarise from Linux with an Apple Developer account.

---

## 6. Reproducibility controls

| What | Pinned by |
|---|---|
| Compilers, libc++, debuggers, clang tools, XMake, Python, git-cliff | `pixi.lock`, the same on the host and in both images |
| Linux glibc baseline for artefacts | `sysroot_linux-64 2.28.*` in `pixi.toml` |
| Container base and Wine | Ubuntu digest + apt snapshot date |
| MSVC + Windows SDK (container) | `vsdownload.py --msvc-version … --sdk-version …` |
| MSVC (native) | versioned `.vsconfig` component + `--vs_toolset` |
| C++ dependencies | `xmake-requires.lock`; `add_requires` with exact versions |
| Image identity | tag = hash of Containerfile + `pixi.lock` |
| Version | `set_version()` in `xmake.lua` + annotated git tag `vX.Y.Z`, both written by `release` |
| Artefact provenance | `dist/<preset>/manifest.json`: version, commit, image tag, `pixi.lock` hash, each file's sha256, and what `pixi.lock` doesn't pin: the MSVC/SDK versions msvc-wine used, or the macOS SDK. `artifact` refuses uncommitted changes and starts from an empty build directory |

### 6.1 Pinning policy

Everything that affects build output is pinned exactly. Changing a pin is a deliberate commit (`build(env)` or `build(containers)`), done with the `bump-toolchain` skill.

| Layer | How it's pinned |
|---|---|
| Pixi packages | Every direct dependency in `pixi.toml` uses `==X.Y.Z`. No `*` and no ranges. `pixi.lock` pins exact builds, including transitive dependencies |
| LLVM | All LLVM packages share one version (§7.1 rule 9) |
| XMake | `xmake ==X.Y.Z` in `pixi.toml`, plus `set_xmakever()` in `xmake.lua` |
| C++ dependencies | Exact versions in `add_requires`, plus the committed `xmake-requires.lock` (phase 2 confirms the lock also records the xmake-repo commit) |
| XMake package builds | A separate package directory per `pixi.lock` (§7.1 rule 11) |
| Container images | Base by digest, Pixi binary version, apt snapshot date, msvc-wine commit, and exact MSVC and SDK versions |
| Native MSVC | Versioned `.vsconfig` components plus `--vs_toolset` |

**Not pinned exactly, on purpose:**
- **Host tools** (Pixi, Podman, Git) come from the host's package manager and update with it. `pixi.toml` sets `requires-pixi = ">=0.83"`, and `doctor` checks minimum versions. Exact pins would break on every system update.
- **GPU drivers and the display stack** belong to the host at runtime.

Known gaps, to be closed later if they matter:

- Packages are downloaded at install time, so a fully offline build would need mirrors.
- msvc-wine's Microsoft CDN downloads can disappear, so CI caches the MSVC payload.

---

## 7. XMake configuration

- **`xmake.lua`** sets:
  - `set_languages("c++23")`
  - `set_version("x.y.z")`
  - the highest warning level, always as errors (§7.1 rule 15)
  - `set_policy("package.requires_lock", true)`
  - `set_xmakever(...)`
  - the toolchain definitions and the host lockdown described in §7.1
- **Build directories:** native builds use `build/<preset>/<mode>`, and every container build (container checks and `artifact`) uses `build/container/<preset>/<mode>`. `artifact` then copies the binary to `dist/<preset>/`.
- **Config isolation:** `XMAKE_CONFIGDIR` sits next to each build directory, so native and container configurations never overwrite each other.
- **Package cache:** native builds use `~/.xmake`, and container builds use the named volume `project_n-xmake-cache`.
- **IDE integration:** each native build refreshes `build/compile_commands.json`, which `.clangd` (`CompilationDatabase: build`) reads.
- **First build cost:** every dependency is built from source once per toolchain, including SDL3's X11/Wayland stack. That took about 15–25 minutes per toolchain in S4. The results are cached in `~/.xmake/packages` (or the container volume), and later builds reuse them.

### 7.1 Linux toolchain rules (proven in S4)

Each rule below fixed a concrete failure or host leak found in S4. `containers/AGENTS.md` and `tools/AGENTS.md` carry the one-line versions.

**Toolchains**

1. **Custom toolchains `pixi-gcc` and `pixi-clang` are defined in `xmake.lua`.** They set `cc`, `cxx`, `ld`, `sh`, `ar`, `strip` and `as` to the `x86_64-conda-linux-gnu-*` tools. XMake's built-in `--toolchain=gcc`/`clang`, with or without `--cross` or `--sdk`, **silently uses the host's `/usr/bin` compilers**. The toolchain asserts that `CONDA_PREFIX` is set, so running outside `pixi run` fails loudly.
2. **`pixi-clang` adds `-stdlib=libc++` and `-L$CONDA_PREFIX/lib`.** libc++ lives outside the sysroot's search path.

**Static C++ runtime**

3. **The static runtime is linked on the project's own targets, never in the toolchain.** Autotools passes XMake's link flags to dependencies' shared libraries too, which broke them.
   - All presets: `add_ldflags("-static-libstdc++", "-static-libgcc")`.
   - Clang additionally: `-Wl,--whole-archive,$CONDA_PREFIX/lib/libc++abi.a,--no-whole-archive -lpthread`. Clang's `-static-libstdc++` adds libc++ but not libc++abi, and whole-archiving makes link order irrelevant.
4. **conda's GCC hard-codes `-rpath <env>/lib` in its built-in link settings.** No flag can turn it off, so `artifact` runs `patchelf --remove-rpath`. Native development builds keep the rpath, which is harmless there.

**Host lockdown**

5. **No library dependency may resolve from the host.** `add_requireconfs("**|<build tools>", {system = false})` makes every library come from xmake-repo. Without it, XMake used pacman's `fmt`, `libx11`, `libffi` and others. `"*"` alone covers only top-level requires.
   - Build tools are excluded from the rule so they can come from Pixi.
   - In S4 XMake still built most tools itself (Python, CMake, meson, ninja, bison, m4). Since phase 2 it uses Pixi's for all of them: checked 2026-10-30, the package directory holds only empty bookkeeping entries for the tools, and the build logs run `.pixi/envs/default/bin/cmake`. The exclusions stay; without them XMake would build its own tools again.
6. **Pixi's activation env is overridden on linux-64** (`[target.linux-64.activation.env]`):
   - Clear `CFLAGS`, `CXXFLAGS`, `CPPFLAGS`, `LDFLAGS` and the `DEBUG_*` variants. The compiler packages export conda-build flags (`-O2 -march=nocona`, an rpath, `-isystem <env>/include`), and CMake-based package builds pick them up.
   - Set `PKG_CONFIG=pkg-config.bin` and `PKG_CONFIG_LIBDIR=/dev/null`. conda's pkg-config wrappers append the host's `/usr/lib/pkgconfig` (the triplet wrapper does this when `CC` is Clang) and the environment's own `.pc` files. An empty `PKG_CONFIG_LIBDIR` counts as unset, hence `/dev/null`.
   - Set `PKG_CONFIG_PATH_FOR_BUILD=$CONDA_PREFIX/share/pkgconfig:$CONDA_PREFIX/lib/pkgconfig`. XMake always runs meson in cross mode, and this is how meson finds build-machine tools such as `wayland-scanner`. Target libraries are unaffected.
   - Set `CC_FOR_BUILD`, `CXX_FOR_BUILD` and `AR_FOR_BUILD` to the `x86_64-conda-linux-gnu-*` tools. In cross mode meson also needs a build-machine compiler and `ar`. Without `AR_FOR_BUILD` it silently used the host's `/usr/bin/ar`, which surfaced only in the clean container (S6). Setting all three also stops the compiler activation scripts' order from deciding which compiler is used.

**Package fixes**

7. **Local package overrides fix xmake-repo packages that only build because the host fills a gap.** Phase 2 moves them into an in-repository package repository (`xmake/repo/`, registered with `add_repositories`), and each one is offered upstream:
   - `libsdl3-wayland` inherits `libsdl3` and adds `libxkbcommon` and `libglvnd` (for `egl.pc`). Without them SDL **silently** builds without Wayland.
   - `libxkbcommon-fixed` inherits `libxkbcommon` and adds `libxml2`.

**MSVC**

12. **MSVC 14.50 has no `/std:c++23`.** It warns (D9002), ignores the option and compiles as **C++14**. `/WX` doesn't make D9002 fatal, and XMake's flag check accepts it. `xmake.lua` therefore passes `/std:c++23preview` on Windows instead of `set_languages("c++23")`. To catch any future silent downgrade on any compiler, the project's core header carries a guard: `#if` on `__cplusplus` (or `_MSVC_LANG`) `<= 202002L` → `#error`. S1 verified that this fails under C++20 and passes with `/std:c++23preview`.

13. **Windows links the C++ runtime statically** with `set_runtimes("MT")`, matching Linux (O4). Players need no Visual C++ Redistributable. XMake passes the runtime to packages and includes it in their cache key, so dependencies always match.

**Warnings**

15. **Every build uses the highest warning level, always as errors**, not just in `check`: `set_warnings("everything", "pedantic", "error")`. That's `-Weverything` for Clang, `-Wall -Wextra -Weffc++ -Wpedantic` for GCC, and `/Wall /WX` for MSVC. The only exclusions report on the compiler rather than our code. For Clang, the C++98 and pre-C++20 compatibility groups. For MSVC, `/Wall`'s codegen notes: padding (C4820), inlining (C4710/C4711), unused inlines removed (C4514) and Spectre mitigation points (C5045). Measured on the hello window before the exclusions: Clang 6 warnings (all compatibility), GCC 0, MSVC 15 (all C4820, all in SDL headers).
16. **Dependencies never produce warnings.** GCC and Clang see package headers through `-isystem`. MSVC uses `/external:anglebrackets /external:W0`, because XMake's own `/external:I` paths don't match under msvc-wine. Our headers use `"..."` and libraries `<...>`, which is what makes this work: with C4820 re-enabled, SDL's headers produced 0 warnings, against 15 before. clang-tidy reports only headers matching `HeaderFilterRegex` (`src/`) and skips system headers.

**Sanitizers**

17. **Sanitizers are available on request, never part of the gate:** `project_n build|test|run --sanitizer <name>`, or `check --sanitizers` for the full matrix. Support per preset is `SANITIZERS` in `project_n.py`: GCC and Clang have address, undefined, thread and leak, and MSVC has address only, since it has no others. Each sanitizer builds into `build/<preset>/<mode>-<sanitizer>`. The rules, each from a failure in phase 2:
    - **Set per target** by a `sanitizer` rule, not with a project-level policy. XMake passes a project-level sanitizer to every package, and `asan = false` in `add_requireconfs` can't stop it: a dependency's `false` is replaced by its parent's value. Wayland's shared library then failed to link instrumented. Dependencies stay uninstrumented, which gives no false positives for ASan and UBSan and means no package rebuilds.
    - **The C++ runtime is linked dynamically in sanitizer builds.** Sanitizer runtimes replace parts of it, and the whole-archived static `libc++abi` collided with TSan's `__cxa_guard_*`. Clang gets an rpath to the environment, and conda's GCC adds its own.
    - **Findings fail the run:** `UBSAN_OPTIONS=halt_on_error=1` (UBSan only reports by default), and `xmake test --output-on-failure` shows the report.
    - **Verified on Linux:** with GCC 16.2 and Clang 22.1.8, each sanitizer passes a clean run and catches its bug class (heap-buffer-overflow, signed overflow, data race, leak), including through `project_n test`.
    - **MSVC ASan builds and links in the container but can't run under Wine.** Its interception hot-patches system DLLs, and Wine's start with CET `endbr64` prologues it can't patch. It runs on native Windows, which is unverified until S5. The ASan runtime is `clang_rt.asan_dynamic-x86_64.dll` even with `/MT`.
    - **MemorySanitizer is excluded:** it needs every library instrumented, libc++ included, and conda-forge ships no MSan-built libc++. Adding it means building an instrumented libc++ as a package.

**Running XMake**

**Debuggers**

8. **gdb pretty-printers:** `tools/debug/gdbinit` registers the libstdc++ printers from `$CONDA_PREFIX/share/gcc-*/python`. gdb never auto-loads them, because libstdc++ is linked statically. `tools/env/activate.sh` appends a `source` line for it to `$CONDA_PREFIX/etc/gdbinit` (the system init file this gdb reads on every start, which the gdb package already uses), replacing any stale line, so the terminal, editors and the container all get it without per-user setup. Verified in S4: `std::vector of length 4, capacity 4 = {1, 2, 3, 4}`.
9. **lldb's libc++ formatters are compiled in and follow libc++'s internal layout.** lldb 22 shows libc++ 22 containers but not libc++ 23 ones (verified in S4). All LLVM packages (Clang, libc++, lld, clang-tools, lldb) must therefore share one version: the newest one for which conda-forge ships *all* of them. **Decided 2026-10-29: LLVM 22.1.8.**

**Running XMake**

10. **`project_n.py` doesn't pipe XMake's output through filters.** In S4, two piped `xmake f` runs stalled with no child processes while holding package locks, and file-redirected runs never did. The cause is unconfirmed. XMake writes to the terminal, and to a log in the build directory when needed.

**Package installs**

14. **`xmake f` runs with `-v`, which makes XMake install packages one at a time (otherwise 4 at once).** For SDL3, XMake requests libxcb and libx11 twice, once as a private dependency. The two requests share one build hash, so they install concurrently into the same directory and corrupt each other ("install: … No such file or directory", phase 2). Every spike run that succeeded used `-v`. This is a `shortcut:` in `project_n.py`, to drop once XMake de-duplicates these requests (to report upstream).

**Package cache**

11. **XMake's package cache is keyed on the toolchain *name*, not the compiler version.** After the LLVM 23 → 22 switch, it silently reused packages built with Clang 23. `project_n.py` therefore sets `XMAKE_PKG_INSTALLDIR=~/.xmake/project_n/<hash of pixi.toml + pixi.lock>/packages` for every XMake call, on every host (until phase 4 the Linux-only activation script did, so Windows shared one directory). `pixi.toml` is included because activation variables (rule 6) change builds but aren't in the lock. Any change to the environment starts a fresh package directory, and `project_n clean --packages` removes old ones.

---

## 8. Build tool: `tools/project_n.py`

Constraints:

- **Single file, stdlib only.** It is run through the Pixi task `project_n`.
- **Thin.** It orchestrates XMake, Podman, git and git-cliff, and anything those tools already do stays there.
- **Mechanical steps live here.** Skills call these commands instead of embedding shell sequences (§10.4).

| Command | Does |
|---|---|
| `doctor` | Checks that the Pixi environment matches the lock, the hook path is set, Podman, Windows toolsets, the WSL path gotcha and images. For each missing tool, prints the §5.0 install command for the current OS. Never installs anything. |
| `build --preset P [--mode M]` | Native build and refreshes `build/compile_commands.json`. |
| `test --preset P [--mode M] [--target T] [-- <doctest args>]` | Runs `xmake test`. With `--target`, runs that test executable and forwards the doctest arguments, for example `--test-case=*physics*`. |
| `run --preset P [--mode M] [--debug]` | Builds and launches the game natively. `--debug` launches it under gdb or lldb (`xmake run -d`). |
| `check [--container]` | The merge gate: `commit-check` on unpushed commits, `fmt --check`, `lint`, then build and test for every preset. It runs natively where possible, and `--container` runs everything in the images. |
| `artifact --preset P` | Clean-room release build in the preset's container (committed sources only, empty build directory), plus the Linux dependency check (§5.2). Writes `dist/<preset>/` and `manifest.json`. |
| `release [--dry-run]` | Requires a clean `main` that is up to date. Runs `git cliff --bumped-version`, writes `set_version()`, regenerates `CHANGELOG.md`, commits `chore(release): vX.Y.Z` and creates the annotated tag `vX.Y.Z`. Never pushes. |
| `commit-check <msg-file> \| --range A..B` | Validates commit messages (§11). |
| `fmt [--check]` / `lint [--fix]` | Runs clang-format and clang-tidy from the Pixi environment. `lint` always passes `--warnings-as-errors=*`, so any finding fails `check`. `--fix` applies clang-tidy's fixes and reformats; whatever remains is fixed by hand before a branch is finished (root AGENTS.md). |
| `image [--preset P]` / `shell --preset P` / `clean [--preset P] [--packages]` | Builds an image if missing / opens a debugging shell in a container / removes build directories (`--packages` also removes XMake package directories for locks other than the current one). |

Behaviour:

- **Container re-entry.** Container commands run `podman run … pixi run project_n <cmd>`. Inside the image, `PROJECT_N_IN_CONTAINER=1` is set, so the script runs XMake directly instead of starting another container.
- **Presets and allowed scopes** are dicts at the top of the file. They are the single source of truth.
- **Self-check.** `tools/test_project_n.py` uses `unittest` to cover preset resolution, command assembly, commit validation and the release version and tag logic, with git-cliff stubbed.

Rejected alternatives:

- Shell scripts: two dialects to maintain.
- Make or Just: one more tool to install.
- XMake plugins: Podman orchestration in Lua is awkward.
- Pixi tasks alone: presets and container re-entry need real logic.

---

## 9. Testing strategy

- **Framework:** **doctest**, from `xmake-repo` with an exact version.
  - It compiles quickly, which matters for a large suite.
  - Each test executable has one `main`, from `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`.
- **XMake integration:** `add_tests()` on each test target, and `xmake test` runs them on every preset.
- **Layout:** `tests/unit/<area>/…` mirrors `src/`, with one test executable per area to keep link times down.
- **Where tests run:**
  - Day to day: in the native Pixi environment.
  - `check`: all presets.
  - `check --container`: Linux presets in `project_n-linux`, Windows under Wine.
  - Release gate: native Windows.
- **Sanitizers:** on request, per §7.1 rule 17.
- **Later:** headless graphics smoke tests (Mesa llvmpipe and lavapipe) and fuzzing, each with its own design note.

---

## 10. Agent knowledge: AGENTS.md, skills and design docs

### 10.1 Three tiers, no duplication

| Tier | Holds | Loaded | Size rule |
|---|---|---|---|
| **AGENTS.md** | Facts an agent needs for *any* task in the area: map, commands, one-line rules, pointers to skills | Always, when working in that directory | ≤ ~100 lines; one line per rule |
| **Skills** (`.claude/skills/<name>/SKILL.md`) | *How* to do a specific job: steps, checklists, examples, detailed rules, gotchas for that job | On demand, when the task matches the skill's `description` | SKILL.md ≤ ~200 lines; longer reference material goes in a `references/` file inside the skill |
| **Design docs** (`docs/design/`) | *Why* something is the way it is; alternatives rejected | When changing the design | No limit; superseded rather than edited once accepted |

**Rule:** a fact lives in exactly one tier. The other tiers link to it instead of restating it. When a rule in AGENTS.md needs more than one line of explanation, the explanation moves to a skill and AGENTS.md keeps the one-line rule plus `→ <skill>`.

### 10.2 Placement

| File | Covers |
|---|---|
| `/AGENTS.md` | Project map, canonical commands, global one-line rules, skill index, contract summary |
| `/containers/AGENTS.md` | Images and their roles, licence constraint, Linux linking rules, Podman/WSL gotchas |
| `/tools/AGENTS.md` | `project_n.py` rules (stdlib only, thin, presets and scopes as single source of truth), running its tests |
| `/src/AGENTS.md` | C++ conventions (filled in by the architecture doc) |
| `/tests/AGENTS.md` | doctest conventions, layout, running and filtering tests |

Every directory with an AGENTS.md also has a `CLAUDE.md` containing only `@AGENTS.md`.

### 10.3 Root AGENTS.md draft

```markdown
# project_n: agent guide

C++23 game (headers, no modules), XMake, doctest. All tools come from Pixi (pixi.toml/pixi.lock).
Never use host compilers. Containers are only for artefacts and container checks.

## Map
- `src/` game and engine · `tests/` doctest suites · `tools/project_n.py` the only entry point
- `containers/` artefact images · `docs/design/` rationale. Read the relevant doc before redesigning anything.

## Commands
- `pixi run project_n doctor` | `build|test|run --preset linux-gcc|linux-clang|windows-msvc`
- `pixi run project_n check`: the merge gate. A change is done when it passes.

## Rules
- Commits: Conventional Commits; `feat`/`fix` only for game code; scopes from `SCOPES` in tools/project_n.py → git-commit skill
- History is linear: rebase, never merge commits.
- New C++ deps: exact version in `add_requires` and commit `xmake-requires.lock`. New tools: add to pixi.toml and commit `pixi.lock`.
- Don't call podman, xmake or git-cliff directly; use project_n.py.
- Missing host tool: run `doctor` and give the user its install instructions. Never install system packages yourself.

## Skills (.claude/skills)
- git-commit: writing commit messages, splitting changes, breaking changes
- agent-docs: editing AGENTS.md or skills, proposing new skills

## Keeping this true
Update AGENTS.md and skills in the same commit as the change that affects them.
Log non-obvious fixes under Gotchas. Propose a skill when a workflow repeats → agent-docs skill

## Skill candidates
| Workflow | Evidence | Proposed skill |
|---|---|---|

## Gotchas
```

Nested AGENTS.md files use the same skeleton (Scope, Rules, Commands, Skills, Gotchas, Skill candidates). They are written in phase 2 with the code they describe.

### 10.4 Using skills well

The `agent-docs` skill encodes these rules:

1. **The description is the trigger.** A skill's front-matter `description` states *when* to use it, using the words a task would contain ("commit", "release", "bump gcc"). A vague description means the skill never loads.
2. **Procedures call commands.** A skill says "run `pixi run project_n release --dry-run`, then check X". It doesn't re-implement steps in shell. If a skill grows mechanical steps, they move into `project_n.py`, and the skill shrinks.
3. **Progressive disclosure.** SKILL.md holds the steps and the decisions an agent must make. Long tables, examples and templates go in `references/*.md` inside the skill, linked from SKILL.md.
4. **Each skill has an owning area.** A skill lists the AGENTS.md that points to it, and that AGENTS.md lists the skill in its Skills section. Adding or removing a skill updates both in the same commit.
5. **Skills are kept true too.** If following a skill fails or misleads, fix the skill in the same commit as the work, and add the failure under the skill's own Gotchas.
6. **Skills hold no secrets or machine-specific paths.** They are committed and shared.
7. **Creation needs approval.** Agents *propose* skills by adding a row to Skill candidates and telling the user. Only the two initial skills (§10.5) are created without that step, as part of phase 2.

### 10.5 Skill plan

**Created in phase 2.** Their content is already known:

| Skill | Owning AGENTS.md | Contents |
|---|---|---|
| `git-commit` | root | The full commit spec from §11: types, scope table pointer, rules, breaking-change criteria, fixup-and-rebase workflow, examples, how to split a mixed change |
| `agent-docs` | root | The three-tier rule (§10.1), the full self-update contract, the skill authoring rules (§10.4), AGENTS.md and SKILL.md templates, how to propose a skill |

**Created in phase 3,** once the commands exist and each procedure has been run once:

| Skill | Owning AGENTS.md | Contents |
|---|---|---|
| `release` | root | Choosing when to release, `release --dry-run` review, SemVer meaning for this project (§11.3), pushing tag and branch, attaching artefacts |
| `bump-toolchain` | containers | Every pin location (`pixi.toml` including the sysroot and the LLVM lockstep rule, Ubuntu digest and snapshot, msvc-wine versions, `.vsconfig`, `--vs_toolset`), the update order, `check --container`, commit type `build(env)` or `build(containers)` |
| `add-dependency` | root | `add_requires` with an exact version, updating the lock, Linux linking rules (§5.2), recording the licence, verifying all presets, `build(deps)` |

**Backlog**, proposed when the trigger is met:

| Skill | Owning AGENTS.md | Trigger |
|---|---|---|
| `fix-xmake-package` | root | ✅ **Created 2026-10-30** (trigger met in S4: two overrides). Covers: diagnosing a package that only builds with host files (find the hidden dependency in the failed build's log, check SDL-style silent feature drops), writing a `set_base` override in `xmake/repo/`, and preparing the upstream xmake-repo change. Propose for creation in phase 2 |
| `add-unit-test` | tests | The first area with more than a handful of tests |
| `design-doc` | root | The second design doc (template, numbering, status lifecycle) |
| `new-subsystem` | src | After the architecture doc |
| `msvc-wine-triage` | containers | 5+ Wine/MSVC gotchas |
| `ci-failure` | (CI AGENTS.md) | Once CI exists |

---

## 11. Commits and versioning

This section is the specification for the `git-commit` and `release` skills. Once those skills exist they are canonical, and this section records only the decision.

### 11.1 Commit format

[Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/): `<type>[(<scope>)][!]: <description>`, then an optional body and footers.

| Type | Use for |
|---|---|
| `feat` / `fix` | Game or engine behaviour (`src/`) only, so the changelog lists what players get |
| `perf` | A performance change with no behaviour change |
| `refactor` | A code change that is neither a fix nor a feature |
| `test` | Tests only |
| `build` | `xmake.lua`, `pixi.*`, `containers/`, `tools/`, dependencies, including tooling features and fixes |
| `ci` | CI configuration |
| `docs` | Docs, design docs, AGENTS.md, skills |
| `style` | Formatting only |
| `chore` | Anything else, including release commits |
| `revert` | Reverts. The body names the reverted commit's hash |

**Scopes** are optional, but when present they must be in `SCOPES` in `tools/project_n.py`. The initial set is `repo`, `env`, `containers`, `tools`, `xmake`, `deps`, `tests`, `design`, `agents` and `release`. The architecture doc adds one scope per `src/` subsystem. A new scope is added in the same commit that first uses it.

**Enforced by `commit-check`:**

- known type, known scope or none
- header at most 72 characters, lowercase imperative description, no trailing period
- blank line before the body
- breaking changes marked with `!` or a `BREAKING CHANGE:` footer
- `fixup!`/`squash!` allowed locally, rejected by `check` and in CI ranges
- merge commits rejected

**Convention only:** wrap the body at 72 characters, and make one logical change per commit.

### 11.2 History

- Linear history.
- Feature branches rebase onto `main` and merge fast-forward, or by rebase-merge on GitHub.
- Use `git commit --fixup` plus `git rebase --autosquash` to tidy a branch before merging.
- Once GitHub hosts the repository, branch protection on `main` requires linear history and allows only rebase merging.

### 11.3 Versioning

- **SemVer** `MAJOR.MINOR.PATCH`, starting at `0.1.0`. The git tag `vX.Y.Z` and `set_version()` always match.
- **Before 1.0:**
  - Breaking changes bump MINOR.
  - `feat` bumps MINOR.
  - `fix` and `perf` bump PATCH.
  - git-cliff's `[bump]` settings in `cliff.toml` encode this.
- **From 1.0:**
  - Breaking changes bump MAJOR.
  - In this project "breaking" means incompatible save files, config or mod/content formats, or a changed build interface (`project_n.py` commands, presets).
- Any other commit type bumps PATCH (git-cliff's behaviour), and git-cliff lists it in the changelog under its own group. Releases are cut deliberately with `project_n release`, so a docs-only patch bump only happens if someone asks for it.
- `CHANGELOG.md` is regenerated by `release` and never edited by hand.

### 11.4 Examples

```
docs(design): add 0001 repository and build environment
build(env): pin gcc 15.2 and clang 21.1
build(tools)!: rename `dist` command to `artifact`

BREAKING CHANGE: scripts calling `project_n dist` must use `project_n artifact`.
feat(render): recreate swapchain on window resize
chore(release): v0.2.0
```

---

## 12. CI

GitHub Actions (`.github/workflows/ci.yml`, since phase 4) only runs `project_n` commands, split into parallel jobs (`check` stays the local gate). The repository is private on GitHub Free, so minutes are metered (Windows 2x, macOS 10x): branch pushes run the Linux jobs, while pushes to `main` and tags run the Windows and macOS jobs. `main` only receives commits that already passed the Linux jobs on their branch. A manual run (`workflow_dispatch`) runs everything on any branch.

| Job | Runner | Runs |
|---|---|---|
| `lint` | ubuntu-26.04 | `commit-check --range`, `doctor --docs`, `fmt --check`, `lint --container` |
| `container (<preset>)` | ubuntu-26.04 | `artifact` for `linux-gcc`, `linux-clang` and `windows-msvc` (msvc-wine): clean-container build, tests, dependency check, `dist/` upload |
| `windows` | windows-2025-vs2026 | native `windows-msvc` tests in release and under ASan, after adding the pinned toolset from `buildtools.vsconfig` |
| `macos` | macos-26 | native `macos-clang` tests in release and under ASan, then `artifact` |

- **Caches**, in two layers:
  - Setup, saved as soon as its step finishes: the Pixi environments (setup-pixi), the `linux` and `msvc` images in GHCR by content tag (`PROJECT_N_REGISTRY`), the MSVC install for msvc-wine (after `project_n image`), the MSVC 14.50 toolset on Windows, and the macOS XMake bundle.
  - XMake packages per job, saved at job end even after a failure.
- **Licence:** no image contains Microsoft's files. They do sit in Actions caches (the MSVC install and the Windows toolset); fork PRs of a public repository can read those, which was accepted on the basis that the repository goes private once setup is done.
- **Branch protection** on `main` is configured for linear history (§11.2) and the Linux jobs, but GitHub Free doesn't enforce it on private repositories; the local `check` and CI results are the gate. Upgrading to GitHub Pro would enforce it.

---

## 13. Risks and spikes (to do before phase 2)

| # | Spike | Pass criteria | Fallback |
|---|---|---|---|
| S4 | **XMake + Pixi toolchains** — ✅ **passed 2026-10-29** (§13.1) | XMake uses the conda-forge GCC and Clang with libc++, not the host's. A C++23 hello-window with one xmake-repo dependency and a doctest test builds, opens a GPU-accelerated window on the host Wayland session, and is debuggable with gdb and lldb from the environment | XMake's `llvm` package toolchain for Clang, plus host GCC with version checks in `doctor` |
| S6 | **Portable Linux artefact** — ✅ **passed 2026-10-29** (§13.2) | The hello-window from `artifact --preset linux-clang` and `--preset linux-gcc` shows no Pixi-environment libraries in `ldd`. Its tests pass in a plain Rocky/Alma 8 container (glibc 2.28) with no Pixi installed | Raise the baseline or bundle the offending library, then document it in `containers/AGENTS.md` |
| S1 | XMake + msvc-wine — ✅ **passed 2026-10-29** (§13.3) | `xmake f -p windows --toolchain=msvc` finds msvc-wine, builds the hello-window and runs its doctest under Wine | Custom toolchain definition in `xmake.lua`, then the §5.6 fallback |
| S5 | Pixi on Windows — ✅ **passed 2026-10-30** in CI (`windows-2025-vs2026`) | The `win-64` environment provides XMake, Python, the Clang tools and git-cliff, and XMake finds the pinned VS toolset | Native XMake install at the pinned version, checked by `doctor` |
| S2 | Bit-identical artefacts — ✅ **passed 2026-10-30** for Linux across machines; Windows after `/Brepro` (§13.2) | Two clean `artifact --preset linux-gcc` runs on different machines match, or the differences are explained (add `-ffile-prefix-map` if needed) | Document "same toolchain" reproducibility rather than "bit-identical" |
| S7 | macOS toolchain (§5.7) — ✅ **passed 2026-10-30** in CI (`macos-26`), with conda's libc++ shipped as a dylib rather than static | On a Mac or CI runner: the `osx-arm64` environment resolves from the same `pixi.toml`, XMake builds the hello-window with conda Clang and static libc++ against the Command Line Tools SDK, the window opens with SDL3 (Cocoa/Metal), doctest passes, and `otool -L` shows only system libraries | Link the system libc++ and raise `MACOSX_DEPLOYMENT_TARGET` until C++23 features are available |
| S3 | Podman on Windows | `artifact --preset linux-gcc` works from PowerShell with podman machine | Document "Windows hosts build Linux artefacts from a WSL shell" |

### 13.1 S4 results

Built on CachyOS (niri Wayland session) with the pins in §3. The spike code is on the `spike/toolchains` branch, which is never merged.

| Check | `pixi-gcc` (GCC 16.2) | `pixi-clang` (Clang + libc++; first 23.1.4, re-verified on 22.1.8) |
|---|---|---|
| C++23 (`<expected>`, `<print>`, ranges) + `fmt` built from source + doctest via `xmake test` | ✓ | ✓ |
| SDL3 3.4.16 window, native Wayland | ✓ OpenGL | ✓ OpenGL and Vulkan |
| SDL3 window, X11 (XWayland) | ✓ | ✓ Vulkan |
| Runtime dependencies of the release binary | libc, libm, libdl, libpthread | libc, libm, libdl, libpthread |
| Highest glibc symbol needed | 2.27 | 2.27 |
| rpath | conda's, removed by `patchelf` | none |
| gdb 18.1: breakpoint, locals, backtrace | ✓, readable containers with the auto-hooked gdbinit (§7.1 rule 8) | ✓ |
| lldb 22.1.8 | ✓ | ✓ with LLVM 22.1.8 (couldn't display libc++ 23 containers, §7.1 rule 9) |

**Findings now encoded in the design:** §7.1 rules 1–11. Most were host leaks or silent fallbacks that would only have surfaced later, as "works on my machine, fails in the container".

**Left for later:**
- SDL picks XWayland by default on niri. Native Wayland needs `SDL_VIDEO_DRIVER=wayland`, or a compositor with the newer protocols SDL prefers. The architecture doc decides the default (with O2).
- Whether Pixi's build tools can replace XMake's own builds of them (§7.1 rule 5).

### 13.2 S6 and S2 results

The `artifact` flow was run by hand in `project_n-linux` (the spike's `containers/linux.Containerfile`): a clean-room build of both Linux presets, `xmake test`, `patchelf --remove-rpath`, then an `ldd` check that fails on any library in the Pixi environment.

**What the clean container caught,** none of which appeared on the host: the build-machine `ar` leak (§7.1 rule 6), uutils coreutils breaking libtool (§5.2), and `pixi.toml` missing from the package-cache key (§7.1 rule 11). This confirms `check --container` as the merge gate: host builds can't see host leaks.

| Check (both presets) | Result |
|---|---|
| Clean-room build + doctest in `project_n-linux` | ✓ |
| `ldd` after rpath removal | glibc libraries only (libc, libm, libdl, libpthread) |
| Rocky Linux 8, glibc 2.28, no Pixi: `hello`, doctest suite, SDL3 window (offscreen driver) | ✓ all run |
| Container-built window on the CachyOS host, native Wayland | ✓ |
| **S2:** two clean-room rebuilds in fresh containers, same machine and package cache | ✓ all 6 binaries bit-identical |

**S2, full criterion (2026-10-30):** a GitHub runner's container artefacts for `d0e8972` against this machine's for `a869b2c` (the same sources plus `CHANGELOG.md`), each from empty package caches. Both Linux binaries were bit-identical. The MSVC binary differed in 4 bytes, the link timestamp in the PE header and debug directory; `/Brepro` (cl and link) makes it deterministic, and two clean local builds at different times now match. A cross-machine re-check of the Windows artefact waits for CI.

### 13.3 S1 results

Built in `project_n-msvc` (the spike's `containers/msvc.Containerfile`) from the same `xmake.lua` as Linux.

| Check | Result |
|---|---|
| XMake detects msvc-wine `cl.exe` as "Microsoft C/C++ Compiler (x64)" | ✓ |
| doctest, `fmt` and SDL3 built from source with MSVC (CMake + Ninja under Wine) | ✓ |
| C++23 hello + `fmt`, doctest suite, SDL3 window built (`/std:c++23preview`) | ✓ |
| `xmake test` runs the Windows test binary under Wine with no extra setup | ✓ |
| `hello.exe`, `hello_test.exe`, `window.exe` (offscreen driver) run under Wine | ✓ |
| Imports, default `/MD` | Windows system DLLs plus the MSVC runtime (`MSVCP140`, `VCRUNTIME140`, UCRT) |
| Imports with `set_runtimes("MT")` (O4) | `hello.exe`: `KERNEL32.dll` only. `window.exe`: Windows system DLLs only. Packages were rebuilt with `/MT` automatically, and tests and window still pass |

**Found and fixed:** the half-written Wine prefix (§5.3) and MSVC's ignored `/std:c++23` (§7.1 rule 12).

**Image size (phase 2):** `project_n-linux` is 10.1 GB, and `project_n-msvc` adds 1.8 GB. Most of the base is probably Pixi's package cache left next to the installed environment, so clear it after `pixi install`. What looked like a second toolset (14.51) is an empty servicing stub (`Auxiliary/*.props`, 8 KB), but XMake's msvc-wine detection takes the newest folder under `VC/Tools/MSVC` as the toolset (`--vs_toolset` is Windows-only), so its include and library paths named 14.51; compiles only used 14.50 because msvc-wine's wrappers set their own. `ensure_msvc` removes toolset folders without `bin/`. The side-by-side 14.50 component is Microsoft's pin; patch levels inside it (ASan 14.50.35734, SDK 10.0.26100.18) follow Microsoft's current manifest at install time, so `manifest.json` records what msvc-wine used.

**Cosmetic (phase 2):** Pixi in the container warns that it found the host's `/src/.pixi/envs` through the bind mount and ignores it. The host environment isn't modified.

---

## 14. Open questions

None open. Deferred to the architecture doc:

- **O2:** which Linux libraries are bundled (xmake-built static libraries such as SDL) and which are expected from the system (GPU drivers, Vulkan and GL loaders, audio). The SDL3 build in S4 bundles SDL statically and dlopens X11, Wayland, xkbcommon, EGL and GL from the system at runtime. The doc should also decide whether to prefer native Wayland over XWayland (§13.1).

---

## 15. Implementation phases (after go-ahead)

Each phase lists the host tools from §5.0 that must be installed before it starts.

0. **Commit this design doc** as `docs(design): add 0001 repository and build environment`. Commits follow §11 by hand until phase 2. *Install first: Git.*
1. ✅ **Spikes** (Linux part done 2026-10-29: S4, S6, S1 passed; S2 partial) in a scratch branch that is never merged: S4 → S6 → S1 → S2, then S5 → S3. Fix the exact versions.
   - *Install first, on Linux:* Pixi (S4), Podman (S6, S1, S2).
   - *Install first, on Windows:* Pixi, Podman and VS Build Tools (S5, S3).
   - S5 and S3 need a Windows machine. They don't block the Linux work in phases 2 and 3 and can run whenever one is available.
2. ✅ **Skeleton** (no new installs; done 2026-10-29 on `setup/repository`. `check --sanitizers`, `check --container` and all three `artifact` builds pass):
   - `pixi.toml` and `pixi.lock`
   - `tools/project_n.py` with `commit-check` first, plus `.githooks/commit-msg` and the tests
   - `xmake.lua` (`set_version("0.1.0")`) with a hello-window target and one doctest test
   - lint/format configs and `cliff.toml`
   - both containers and the `.vsconfig`
   - all AGENTS.md and CLAUDE.md files
   - the `git-commit` and `agent-docs` skills
3. **Workflow green:**
   - ✅ A native build, run and debug works from the editor on this machine (Wayland by default since `defcefe`).
   - ✅ `check` and `check --container` pass.
   - ✅ `artifact` produces Linux and Windows builds.
   - ✅ The native Windows build runs (CI `windows` job, 2026-10-30).
   - ✅ The first `release --dry-run` succeeds (2026-10-30, would release v0.1.0).
   - ✅ Then create the `release`, `bump-toolchain` and `add-dependency` skills. Licences are recorded as comments on each `add_requires` line.
4. ✅ **CI and macOS** (2026-10-30): GitHub repository, workflows and caches (§12), branch protection, `doctor --docs`, the `macos-clang` preset (§5.7). Backlog skills follow as their triggers fire.

---

## Appendix A: alternatives considered

| Option | Why not (for now) |
|---|---|
| uv only | Can't provide C++ toolchains. Pixi uses uv internally for PyPI packages |
| Host compilers + apt toolchains in containers | Two sources of compilers drift apart |
| mise / asdf | No prebuilt GCC; toolchains would be assembled from scattered release tarballs |
| Nix / Guix | No Windows support |
| Steam Runtime SDK as the Linux artefact base | Dropped. It's a second environment alongside Pixi, and shipping on Steam isn't a requirement. Revisit if a Steam release needs it, since Steam runs plain glibc-baseline binaries anyway |
| Dev Containers / distrobox for daily work | Development stays on the host |
| Docker | Not rejected for function. Podman is preferred because it's daemonless and rootless |
| CMake | XMake was a stated choice |
| clang-cl + xwin instead of msvc-wine | It isn't MSVC. Possible later preset |
| Windows containers | See §5.6 |
| C++ modules | Out of scope by decision |
| Catch2 / GoogleTest | doctest chosen for compile speed |
| commitlint / commitizen / cocogitto | An extra dependency for a ~30-line check. git-cliff covers changelog and versioning |
| Merge commits / squash merges | Linear rebase history chosen. Every commit stays validated and visible to git-cliff |

---

## Appendix B: resolved questions (2026-10-29)

| # | Question | Answer |
|---|---|---|
| Q1 | Base image | Ubuntu LTS for all images |
| Q2 | Clang standard library | libc++ |
| Q3 | Test framework | doctest |
| Q4 | Hosting / CI | GitHub eventually; workflows deferred |
| Q5 | Linux baseline | Steam Runtime SDK at first, later dropped in favour of the Pixi/conda-forge sysroot (see O1) |
| Q6 | Editor config | Nothing committed at first |
| Q7 | Accept conda-forge compiler lag | Yes |
| Q8 | Entry point name | `project_n` |
| Q9 | Changelog / versioning | git-cliff, SemVer |
| Q10 | Merge strategy | Linear history, rebase only |
| O4 | Windows C++ runtime | Static (`/MT`), matching Linux |
| O1 | Linux glibc baseline | 2.28 (conda-forge `sysroot_linux-64` 2.28) |
| O2 | Bundled vs system Linux libraries | Deferred to the architecture doc |
| O3 | VS Build Tools licence fits the project | Yes |
