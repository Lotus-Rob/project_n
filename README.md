# Project N

A C++23 game. Builds are reproducible: every compiler and tool is pinned in a portable, project-local
[Pixi](https://pixi.sh) environment, and release artefacts come from clean [Podman](https://podman.io) containers.

## Install

Install each tool when you first need it. Everything else (compilers, XMake, debuggers, formatters, Python) comes from `pixi install`.

| Tool | Needed for | Linux (Arch / CachyOS) | Windows |
|---|---|---|---|
| Git | everything | `sudo pacman -S git` | `winget install Git.Git` |
| Pixi | everything | `sudo pacman -S pixi` (elsewhere: `curl -fsSL https://pixi.sh/install.sh \| sh`) | `winget install prefix-dev.pixi` |
| Podman | artefacts, container checks, Windows builds on Linux | `sudo pacman -S podman` | `winget install RedHat.Podman`, then `podman machine init` and `podman machine start` |
| VS Build Tools | native Windows builds | n/a | Build Tools bootstrapper with `--config containers\windows\buildtools.vsconfig --passive --wait` |

## Set up

```sh
pixi install                            # create .pixi/ from pixi.lock
git config core.hooksPath .githooks     # enable the commit-message check
pixi run project_n doctor               # check everything; prints fixes, installs nothing
```

Point your editor's clangd, clang-format and debugger at `.pixi/envs/default/bin/` (or start the editor from `pixi shell`).
Every native build writes `build/compile_commands.json`, which `.clangd` reads.

## Build, test, run

```sh
pixi run project_n build --preset linux-clang         # presets: linux-gcc, linux-clang, windows-msvc
pixi run project_n test  --preset linux-gcc
pixi run project_n run   --preset linux-clang --debug # launch under gdb
pixi run project_n check                              # the merge gate: commits, format, lint, all presets
```

Native builds land in `build/<preset>/<mode>/`. `windows-msvc` builds in a container on Linux, using the real MSVC under Wine.
Building that image downloads Microsoft's Build Tools: set `PROJECT_N_ACCEPT_MSVC_LICENSE=1` to accept their licence.

## Release

```sh
pixi run project_n artifact --preset linux-gcc   # clean-room build into dist/<preset>/ with manifest.json
pixi run project_n release --dry-run             # next SemVer version and changelog from commit messages
```

## Contributing

Commits follow [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) and history is linear (rebase, no merge commits).
Design and rationale: [docs/design/0001](docs/design/0001-repository-build-environment.md). Agents: see [AGENTS.md](AGENTS.md).
