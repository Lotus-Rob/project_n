# Project N

A C++23 game. Builds are reproducible: every compiler and tool is pinned in a portable, project-local
[Pixi](https://pixi.sh) environment, and release artefacts come from clean [Podman](https://podman.io) containers.

## Install

Install each tool when you first need it. Everything else (compilers, XMake, debuggers, formatters, Python) comes from `pixi install`.

| Tool | Needed for | Linux (Arch / CachyOS) | Windows | macOS |
|---|---|---|---|---|
| Git | everything | `sudo pacman -S git` | `winget install Git.Git` | comes with the Command Line Tools |
| Pixi | everything | `sudo pacman -S pixi` (elsewhere: `curl -fsSL https://pixi.sh/install.sh \| sh`) | `winget install prefix-dev.pixi` | `brew install pixi` (or the `curl` installer) |
| Podman | artefacts, container checks, Windows builds on Linux | `sudo pacman -S podman` | `winget install RedHat.Podman`, then `podman machine init` and `podman machine start` | `brew install podman`, then `podman machine init` and `podman machine start` |
| VS Build Tools | native Windows builds | n/a | Build Tools bootstrapper with `--config containers\windows\buildtools.vsconfig --passive --wait` | n/a |
| Xcode Command Line Tools | native macOS builds | n/a | n/a | `xcode-select --install` |

## Set up

```sh
pixi install                            # create .pixi/ from pixi.lock
git config core.hooksPath .githooks     # enable the commit-message check
pixi run project_n doctor               # check everything; prints fixes, installs nothing
```

Point your editor's clangd, clang-format and debugger at `.pixi/envs/default/bin/` (or start the editor from `pixi shell`).
Every native build writes `build/compile_commands.json`, which `.clangd` reads.

## Build, test, run

`./dev` (Windows: `dev`) is shorthand for `pixi run project_n`. Without `--preset`, `build`, `test` and `run`
use this machine's native preset: `linux-clang` on Linux, `windows-msvc` on Windows, `macos-clang` on macOS.

```sh
./dev build                              # presets: linux-gcc, linux-clang, windows-msvc, macos-clang
./dev build --preset all                 # every preset, past failures, then a pass/fail summary
./dev test --preset linux-gcc
./dev run                                # --debug launches under gdb
./dev check                              # the merge gate: commits, format, lint, all presets
./dev test --sanitizer address           # on request: address, undefined, thread, leak
./dev coverage                           # test coverage of src/: build/coverage/index.html
./dev fuzz smoke --time 60               # libFuzzer targets in tests/fuzz/
./dev docs                               # API reference (Doxygen): build/docs/html/
./dev run --mode releasedbg              # profiling build; ./dev profiler opens the Tracy viewer
```

Native builds land in `build/<preset>/<mode>/`. `windows-msvc` builds in a container on Linux, using the real MSVC under Wine.
Its first use installs Microsoft's Build Tools under `~/.xmake/project_n-tools/`: set `PROJECT_N_ACCEPT_MSVC_LICENSE=1` to accept their licence.
`macos-clang` builds only on a Mac (Apple's licences); elsewhere CI's macOS job covers it, and `check` skips it.

## Release

```sh
pixi run project_n artifact --preset linux-gcc   # clean-room build into dist/<preset>/ with manifest.json
pixi run project_n release --dry-run             # next SemVer version and changelog from commit messages
```

## Licence

Copyright (C) 2026 Rob G. Licensed under the [GNU Affero General Public License v3.0 or later](LICENSE).
Bundled libraries and their licences: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Contributing

Commits follow [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) and history is linear (rebase, no merge commits).
Design and rationale: [docs/design/0001](docs/design/0001-repository-build-environment.md). Agents: see [AGENTS.md](AGENTS.md).
