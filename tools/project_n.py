"""project_n: the single build entry point. Run as `pixi run project_n <command>`.

Thin by design: every command reduces to calls of XMake, Podman, git, git-cliff, gh and the pinned tools.
Standard library only. See tools/AGENTS.md.
"""

import argparse
import ast
import bisect
import collections
import concurrent.futures
import dataclasses
import difflib
import functools
import hashlib
import io
import itertools
import json
import os
import plistlib
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
import tokenize
import tomllib
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HOST = "windows" if os.name == "nt" else "macos" if sys.platform == "darwin" else "linux"
IN_CONTAINER = os.environ.get("PROJECT_N_IN_CONTAINER") == "1"

APP = "project_n"
BUNDLE_ID = "dev.projectn.project-n"  # placeholder macOS bundle id, fix it for good before signing (O6)
MODES = ("debug", "release", "releasedbg")
VS_TOOLSET = "14.50"  # native Windows MSVC; must match containers/windows/buildtools.vsconfig
# conda-forge has no osx-arm64 XMake, so macOS uses the upstream bundle of set_xmakever()'s
# version. Checksum of xmake-bundle-v<version>.macos.arm64.
XMAKE_MACOS_SHA256 = "e719121c56cd34f5f0c0731954a154c1df1b0915f303e5c82dfb6c6cb9e3ef9e"
LINT_PRESET = "linux-clang"  # #ifdef'd Windows/macOS code isn't linted; lint each native preset once some exists

# Sanitizers each preset supports (on request only, never in plain `check`). MSVC has only
# AddressSanitizer. MemorySanitizer is left out: it needs every library instrumented,
# libc++ included, and conda-forge ships no MSan-built libc++.
SANITIZERS = {
    "linux-gcc": ("address", "undefined", "thread", "leak"),
    "linux-clang": ("address", "undefined", "thread", "leak"),
    "windows-msvc": ("address",),
    "macos-clang": ("address", "undefined", "thread"),
}
# UBSan only reports by default; make every sanitizer finding fail the run.
# MSVC's ASan has no LeakSanitizer and fails every run that asks for it.
SANITIZER_ENV = {"UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1",
                 "ASAN_OPTIONS": "abort_on_error=0" + ("" if HOST == "windows" else ":detect_leaks=1")}

# Single source of truth for presets. `host` is the OS that builds natively.
PRESETS = {
    "linux-gcc": {"host": "linux", "plat": "linux", "arch": "x86_64", "toolchain": "pixi-gcc", "image": "linux"},
    "linux-clang": {"host": "linux", "plat": "linux", "arch": "x86_64", "toolchain": "pixi-clang", "image": "linux"},
    "windows-msvc": {"host": "windows", "plat": "windows", "arch": "x64", "toolchain": "msvc", "image": "msvc"},
    # No container: macOS builds only on a Mac (Apple's licences), so CI's macos job covers it.
    "macos-clang": {"host": "macos", "plat": "macosx", "arch": "arm64", "toolchain": "pixi-clang", "image": None},
}
# build, test and run use this preset when --preset is left out.
NATIVE_PRESET = {"linux": "linux-clang", "windows": "windows-msvc", "macos": "macos-clang"}[HOST]

# Images are tagged by a hash of everything they copy in, so changing any of it rebuilds them.
# `push`: may go to PROJECT_N_REGISTRY (CI's image cache). Images hold no Microsoft files: MSVC
# is installed outside them (msvc_dir) because its licence forbids redistribution.
IMAGES = {
    "linux": {"file": "containers/linux.Containerfile", "base": None, "push": True},
    "msvc": {"file": "containers/msvc.Containerfile", "base": "linux", "push": True},
}
# Runs in the msvc image with msvc_dir() at /opt/msvc. vsdownload.py uses the Pixi env's Python.
MSVC_INSTALL = ('pixi run python /opt/msvc-wine/vsdownload.py --accept-license --msvc-version "$MSVC_VERSION" '
                '--sdk-version "$SDK_VERSION" --architecture x64 --dest /opt/msvc && /opt/msvc-wine/install.sh /opt/msvc')
# Installed over msvc-wine's bin/x64/cl.exe (a symlink to its `cl` script) by install_cl_wrapper. Under Wine, the
# /sourceDependencies JSON XMake asks for names headers as lowercased z:\ paths XMake can't find, so every MSVC build
# recompiled everything; without it XMake reads /showIncludes, which msvc-wine's `cl` turns into Linux paths.
CL_WRAPPER = """#!/usr/bin/env bash
# Written by project_n (CL_WRAPPER in tools/project_n.py): msvc-wine's cl without /sourceDependencies and its file,
# when project_n's builds ask for it (xmake_env). Every checkout shares this install, and one whose xmake.lua keeps
# XMake's build cache on needs the option: its /showIncludes lists no headers.
if [[ ${PROJECT_N_CL_DROP_SOURCE_DEPENDENCIES-} != 1 ]]; then
  exec "$(dirname "$0")/cl" "$@"
fi
args=()
while (($#)); do
  case $1 in
    [-/]sourceDependencies) shift ;;  # and its file, the next argument
    *) args+=("$1") ;;
  esac
  (($#)) && shift
done
exec "$(dirname "$0")/cl" "${args[@]}"
"""
IMAGE_INPUTS = ("pixi.toml", "pixi.lock", "tools/env/activate.sh")
IMAGE_IGNORE = "containers/.containerignore"  # the build context is the repo root; this admits only IMAGE_INPUTS
CACHE_VOLUME = "project_n-xmake-cache"  # PROJECT_N_XMAKE_CACHE=<dir> bind-mounts a directory instead (CI caches it)
# CI's compiled objects of our code, one folder per build (object_cache_dir), kept between runs by actions/cache.
# Unset locally, where each build folder keeps XMake's own cache, aged by sweep_build (CACHE_DAYS).
BUILD_CACHE_ENV = "PROJECT_N_BUILD_CACHE"
CONTAINER_BUILD_CACHE = "/home/dev/build-cache"  # where container_cmd mounts it
# Toolchains whose CI builds use the object cache. Not pixi-gcc: with a precompiled header, GCC's preprocessed text
# holds `#pragma GCC pch_preprocess "<pch>.gch"` instead of the headers, so XMake's key (which hashes that text)
# misses a change to any header the PCH includes (core/types.hpp...) and hands back the old object; xmake.lua keeps
# GCC off the build cache everywhere. Not msvc either: a hit drops the debug info /Zi keeps beside the objects, and
# under Wine it is no faster (.github/AGENTS.md, Gotchas).
CI_CACHED_TOOLCHAINS = ("pixi-clang",)

# Conventional Commits. Add a scope in the commit that first uses it.
TYPES = ("feat", "fix", "perf", "refactor", "test", "build", "ci", "docs", "style", "chore", "revert")
SCOPES = ("repo", "env", "containers", "tools", "xmake", "deps", "tests", "design", "agents", "release", "core", "data", "platform", "gfx", "game", "app")

HEADER_RE = re.compile(r"^(?P<type>[a-z]+)(?:\((?P<scope>[a-z0-9-]+)\))?(?P<breaking>!)?: (?P<desc>.+)$")
MAX_HEADER = 72
VERSION_RE = re.compile(r'set_version\("([^"]+)"\)')
XMAKEVER_RE = re.compile(r'set_xmakever\("([^"]+)"\)')
CLIFF = ["git", "cliff", "--config", "tools/cliff.toml"]
# --lsp=clangd keeps packages' -isystem: otherwise XMake writes -I, and clangd and clang-tidy then
# report warnings in third-party headers and in their macros as if they were ours.
COMPDB = ["xmake", "project", "-k", "compile_commands", "--lsp=clangd"]

# Host prerequisites: printed by `doctor`, never installed by it.
INSTALL_HINTS = {
    "pixi": {"linux": "sudo pacman -S pixi  (or: curl -fsSL https://pixi.sh/install.sh | sh)", "windows": "winget install prefix-dev.pixi"},
    "git": {"linux": "sudo pacman -S git", "windows": "winget install Git.Git"},
    "podman": {"linux": "sudo pacman -S podman", "windows": "winget install RedHat.Podman, then: podman machine init; podman machine start",
               "macos": "brew install podman, then: podman machine init; podman machine start"},
    "vs": {"windows": r"vs_BuildTools.exe --config containers\windows\buildtools.vsconfig --passive --wait"},
    "xcode": {"macos": "xcode-select --install"},
}


DOCTOR_PROBLEMS = 3  # doctor's exit code for "problems found", distinct from errors (1) so CI can tolerate only it


class Fail(Exception):
    """A user-facing error: printed without a traceback."""


def resolve(cmd):
    """Look the program up on PATH: Windows' CreateProcess only finds .exe files."""
    exe = shutil.which(str(cmd[0])) or str(cmd[0])
    if HOST == "windows" and cmd[0] == "xmake" and "CONDA_PREFIX" in os.environ:
        # conda's xmake.cmd ends with `endlocal`, which replaces xmake's exit code with 0: failed
        # builds and tests looked green. Run the xmake.exe it wraps.
        exe = str(Path(os.environ["CONDA_PREFIX"], "Library", "share", "xmake", "xmake.exe"))
    args = list(cmd[1:])
    if cmd[0] == "xmake" and args and not str(args[0]).startswith("-"):
        # XMake takes an ancestor folder holding a project over the current one: a worktree inside the main
        # checkout would build the main checkout's sources.
        args[1:1] = ["-P", ROOT]
    return [exe, *(str(c) for c in args)]


def lock_bytes():
    lock = ROOT / "xmake-requires.lock"
    return lock.read_bytes() if lock.is_file() else b""


LOCK_TOKEN = re.compile(r'\s*(\[\s*"(?:[^"\\]|\\.)*"\s*\]|"(?:[^"\\]|\\.)*"|[{}=,]|[\w.+-]+)')


def lock_entries(data):
    """xmake-requires.lock's entries, {(platform, package): fields} plus {(key,): value} for top-level keys
    that hold no packages (`__meta__`), with fields as canonical JSON; None if the text isn't the Lua table
    XMake writes."""
    text, pos, tokens = data.decode("utf-8", "replace"), 0, []
    while pos < len(text.rstrip()):
        match = LOCK_TOKEN.match(text, pos)
        if not match:
            return None
        tokens.append(match.group(1))
        pos = match.end()
    stream = iter(tokens + [None])

    def value(token):
        if token != "{":
            if token in (None, "}", "=", ","):
                raise ValueError(token)
            return token
        table = {}
        token = next(stream)
        while token != "}":
            key = token.strip('[] ').strip('"') if token and token.startswith("[") else token
            if next(stream) != "=":
                raise ValueError(key)
            table[key] = value(next(stream))
            token = next(stream)
            if token == ",":
                token = next(stream)
        return table

    try:
        tree = value(next(stream))
        if next(stream) is not None or not isinstance(tree, dict):
            return None
    except (ValueError, StopIteration):
        return None
    entries = {}
    for key, fields in tree.items():
        if isinstance(fields, dict) and key.count("|") == 1:
            entries.update(((key, package), json.dumps(f, sort_keys=True)) for package, f in fields.items())
        else:
            entries[(key,)] = json.dumps(fields, sort_keys=True)
    return entries


def restore_lock_unless_added(before):
    """XMake rewrites xmake-requires.lock after every call: in another order (`__meta__` moves, entries swap:
    Lua table order), and with only the entries the current configuration uses (the windows-msvc container
    drops native Windows' build tools from `windows|x64`: its own host tools are Linux ones). When it only
    reordered or dropped entries, write the bytes from `before` back so `git status` stays clean. An added or
    changed entry stays and prints its diff, to commit: a platform's missing entries, added on hosts no one
    commits from (CI's macos and windows jobs), would otherwise go unseen."""
    now = lock_bytes()
    if now == before:
        return
    old, new = lock_entries(before), lock_entries(now)
    if old is not None and new is not None and new.items() <= old.items():
        (ROOT / "xmake-requires.lock").write_bytes(before)
        return
    print("xmake-requires.lock: XMake added or changed entries; commit them (tools/AGENTS.md):", flush=True)
    old_lines, new_lines = (b.decode("utf-8", "replace").splitlines() for b in (before, now))
    diff = difflib.unified_diff(old_lines, new_lines, "a/xmake-requires.lock", "b/xmake-requires.lock", lineterm="")
    print("\n".join(diff), flush=True)


def run(cmd, env=None, **kwargs):
    """Run a command from the repo root, echoing it; raise on failure. Output is never piped.
    Any XMake call may reorder or prune xmake-requires.lock, so each one restores it afterwards, even when it
    fails (tools/AGENTS.md)."""
    print("+", " ".join(str(c) for c in cmd), flush=True)
    before = lock_bytes() if cmd[0] == "xmake" else None
    try:
        return subprocess.run(resolve(cmd), cwd=ROOT, check=True, env=env, **kwargs)
    finally:
        if before is not None:
            restore_lock_unless_added(before)


def capture(cmd, check=True):
    # UTF-8, not the locale: Windows would decode with cp1252 and fail on XMake's output.
    return subprocess.run(resolve(cmd), cwd=ROOT, check=check, capture_output=True, encoding="utf-8", errors="replace").stdout


def sha256(*paths):
    h = hashlib.sha256()
    for p in paths:
        h.update(Path(ROOT, p).read_bytes())
    return h.hexdigest()


def needs_container(preset, force=False):
    """The location rule: native when this host builds the preset, else its container."""
    if IN_CONTAINER:
        return False
    return force or PRESETS[preset]["host"] != HOST


def buildable_here(preset):
    """Natively, or through its container; macos-clang has none."""
    return bool(PRESETS[preset]["image"]) or (PRESETS[preset]["host"] == HOST and not IN_CONTAINER)


def build_leaf(mode, sanitizer="none", unity=False):
    return (mode if sanitizer == "none" else f"{mode}-{sanitizer}") + ("-unity" if unity else "")


def build_dir(preset, mode, sanitizer="none", unity=False):
    leaf = build_leaf(mode, sanitizer, unity)
    return Path("build", "container", preset, leaf) if IN_CONTAINER else Path("build", preset, leaf)


# Build trees nothing has used for this long are removed at the start of `build`, `test` and `check`: every preset,
# mode and sanitizer keeps a full tree of its own, and nothing else ever removed them (build/ reached 57 GB).
STALE_DAYS = 7
# The native preset's everyday builds, never swept however long they rest.
WARM_LEAVES = ("debug", "release")
# Written into a build tree by each configure: the disk may be mounted noatime, so files' times can't say when a tree
# was last read.
USED_STAMP = ".project_n-used"
# Set by the first sweep of a run: the commands a command runs (check's steps, `build --preset all`'s builds) would
# each keep only their own tree and remove the ones the run builds next.
swept = False


# XMake's build cache in a build folder never evicts (linux-clang debug's held 6.9 GB for 0.7 GB of objects, about 1 GB
# a day), so entries written longer ago than this go. With no read times, an old entry still in use is compiled once
# more.
CACHE_DAYS = 7


def build_trees(build):
    """Every tree a command builds in: build/<preset>/<leaf> and build/container/<preset>/<leaf>."""
    trees = []
    for parent in (build, build / "container"):
        for preset in PRESETS:
            if (parent / preset).is_dir():
                trees += sorted(p for p in (parent / preset).iterdir() if p.is_dir())
    return trees


def mark_used(bdir, now):
    bdir.mkdir(parents=True, exist_ok=True)
    (bdir / USED_STAMP).write_text(repr(now), encoding="utf-8")


def last_used(tree):
    """When a command last configured the tree; for a tree from before the stamps (or a broken stamp), its newest
    file's time."""
    try:
        return float((tree / USED_STAMP).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return max((p.stat().st_mtime for p in tree.rglob("*") if p.is_file()), default=0.0)


def warm_trees(build):
    return {build / NATIVE_PRESET / leaf for leaf in WARM_LEAVES}


def tree_size(tree):
    return sum(p.stat().st_size for p in tree.rglob("*") if p.is_file() and not p.is_symlink())


def uses_build_cache(tree):
    """Whether XMake reads this tree's build cache: Clang only, as xmake.lua keeps it off for GCC and for MSVC. The
    same toolchains as CI_CACHED_TOOLCHAINS today, for another reason: those are the ones worth CI's object cache."""
    return PRESETS[tree.parent.name]["toolchain"] == "pixi-clang"


def age_build_cache(tree, now, days=CACHE_DAYS):
    """Remove the tree's build cache entries written over `days` ago, each `<key>` with its `<key>.txt` (the
    warnings), or the whole cache where nothing reads it; the removed sizes."""
    cache = tree / ".build_cache"
    if not cache.is_dir():
        return []
    files = [p for p in cache.rglob("*") if p.is_file()]
    if not uses_build_cache(tree):
        sizes = [p.stat().st_size for p in files]
        shutil.rmtree(cache)
        return sizes
    cutoff = now - days * 86400
    keys = {p.with_suffix("") if p.suffix == ".txt" else p for p in files}
    old = {k for k in keys if (k.stat().st_mtime if k.exists() else k.with_suffix(".txt").stat().st_mtime) < cutoff}
    removed = []
    for p in files:
        if (p.with_suffix("") if p.suffix == ".txt" else p) in old:
            removed.append(p.stat().st_size)
            p.unlink()
    remove_empty_folders(cache)
    return removed


def sweep_build(build, now, days=STALE_DAYS, keep=()):
    """Remove the build trees unused for `days`, but the warm ones and `keep`; (tree, bytes) for each removed."""
    spared = warm_trees(build) | {Path(k) for k in keep}
    removed, cached = [], []
    for tree in build_trees(build) if build.is_dir() else ():
        # A file another program holds (a running test on Windows) mustn't fail the build that only came to tidy up.
        try:
            if tree in spared or last_used(tree) > now - days * 86400:
                cached += age_build_cache(tree, now)
                continue
            size = tree_size(tree)
            shutil.rmtree(tree)
            if not any(tree.parent.iterdir()):
                tree.parent.rmdir()
        except OSError as e:
            print(f"sweep: left {tree.relative_to(build.parent)}: {e}")
            continue
        removed.append((tree, size))
        print(f"sweep: removed {tree.relative_to(build.parent)} ({size / 2**30:.1f} GiB), unused for {days}+ days")
    if cached:
        print(f"sweep: removed {len(cached)} build cache files ({sum(cached) / 2**30:.1f} GiB) written over {CACHE_DAYS} days ago"
              " or never read")
    if removed:
        print(f"sweep: freed {sum(s for _, s in removed) / 2**30:.1f} GiB")
    return removed


def preset_trees(preset, leaf):
    """One build's tree, native and in the container, relative to ROOT."""
    return [Path("build", preset, leaf), Path("build", "container", preset, leaf)]


def own_trees(presets, mode, sanitizer="none", unity=False, container=False):
    """The trees a command is about to build in, relative to ROOT: each preset's native one, or its container's where
    needs_container sends the command."""
    leaf = build_leaf(mode, sanitizer, unity)
    return [preset_trees(preset, leaf)[1 if needs_container(preset, container) else 0] for preset in presets]


def sweep_here(keep=()):
    """sweep_build on this checkout's build/, once per run and on the host only: CI starts clean, and a container
    re-entry already swept on the host."""
    global swept
    if swept or IN_CONTAINER or os.environ.get("GITHUB_ACTIONS"):
        return
    swept = True
    now = time.time()
    # Stamped now, not when each configures: a sweep from another terminal would otherwise see them stale while
    # this run starts a container or works through check's earlier steps.
    for tree in (ROOT / k for k in keep):
        if tree.is_dir():
            mark_used(tree, now)
    sweep_build(ROOT / "build", now, keep=[ROOT / k for k in keep])


def cold_ref():
    """CI is building main or a tag: every object compiles from source."""
    ref = os.environ.get("GITHUB_REF", "")
    return ref == "refs/heads/main" or ref.startswith("refs/tags/")


def tag_ref():
    return os.environ.get("GITHUB_REF", "").startswith("refs/tags/")


# XMake's key for a cached object (build_cache.lua's cachekey) is the compiler's path, the compile flags without
# -D, -I, -isystem, -include and --sysroot, and a hash of the preprocessed source. Leaving -D out is safe for our
# flags: every define we pass (PN_ASSERTS, _FORTIFY_SOURCE, _GLIBCXX_ASSERTIONS, _LIBCPP_HARDENING_MODE,
# _LIBCPP_DISABLE_AVAILABILITY, NDEBUG, packages' TRACY_*) is an ordinary macro that reaches the object only through
# the preprocessed text, which is hashed; none is a compiler switch. The values the generated build_info.hpp holds
# (the version, the data paths) are in that text too. Clang's text holds the precompiled header's headers as well,
# and its object compiles from that text alone.
def build_cache_root():
    """BUILD_CACHE_ENV made absolute (podman takes a relative source for a volume name; XMake resolves --ccachedir
    from ROOT), or None: unset, or a tag, which neither reads nor writes one."""
    root = os.environ.get(BUILD_CACHE_ENV)
    return Path(root).absolute() if root and not tag_ref() else None


def object_cache_dir(preset, mode, sanitizer="none", unity=False):
    """CI's object cache for one build: <BUILD_CACHE_ENV>/<preset>-<leaf>, or None (unset, a tag, an uncached toolchain)."""
    root = build_cache_root()
    if not root or PRESETS[preset]["toolchain"] not in CI_CACHED_TOOLCHAINS:
        return None
    return root / f"{preset}-{build_leaf(mode, sanitizer, unity)}"


def holds_objects(bdir):
    """Whether a build folder already holds compiled objects: XMake's .objs/, or its own cache in .build_cache/."""
    folders = (ROOT / bdir / ".objs", ROOT / bdir / ".build_cache")
    return any(d.is_dir() and any(p.is_file() for p in d.rglob("*")) for d in folders)


def object_build_dirs(folder):
    """The build folders whose objects a <preset>-<leaf> folder of CI's object cache holds (object_cache_dir): native and
    container; none for a name no preset starts."""
    for preset in PRESETS:
        leaf = folder.removeprefix(f"{preset}-")
        if leaf != folder and leaf:
            return [ROOT / tree for tree in preset_trees(preset, leaf)]
    return []


def file_digest(path):
    with open(path, "rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


# A clang-tidy result written within this many seconds before the run's mark still counts as this run's: Linux stamps
# files from a coarse clock that may trail time.time() by a tick. Results from earlier runs are minutes older.
TIDY_CLOCK_SLACK = 2.0


def prune_objects(folder, every):
    """Remove the files of one object cache folder this run's build didn't use: XMake's build cache never evicts (a hit
    copies `<key>` to the object file and leaves it), so each save grew. Used means byte for byte equal to an object in
    the build folder (a hit copies it there, a miss copies it from there), and `<key>.txt` (the warnings) goes with its
    `<key>`. With `every`, all stay: main wrote into an empty folder (the cold rule). (kept, removed) as
    ([sizes], [sizes])."""
    files = [p for p in folder.rglob("*") if p.is_file()]
    if every:
        return [p.stat().st_size for p in files], []
    objects = [p for d in object_build_dirs(folder.name) for p in (d / ".objs").rglob("*") if p.is_file()]
    sizes = {p.stat().st_size for p in objects}
    used = {file_digest(p) for p in objects}
    keep = {p for p in files if p.suffix != ".txt" and p.stat().st_size in sizes and file_digest(p) in used}
    keep |= {p for p in files if p.suffix == ".txt" and p.with_suffix("") in keep}
    kept, removed = [], []
    for p in files:
        (kept if p in keep else removed).append(p.stat().st_size)
        if p not in keep:
            p.unlink()
    return kept, removed


def prune_tidy(folder):
    """Remove the clang-tidy results this run neither wrote nor read (TIDY_RUN_MARK), and the mark. No mark: lint never
    reached clang-tidy, so none was used."""
    mark = folder / TIDY_RUN_MARK
    try:
        start = float(mark.read_text(encoding="utf-8")) - TIDY_CLOCK_SLACK
    except (OSError, ValueError):
        start = float("inf")
    mark.unlink(missing_ok=True)
    kept, removed = [], []
    for p in (p for p in folder.rglob("*") if p.is_file()):
        size, recent = p.stat().st_size, p.stat().st_mtime >= start
        (kept if recent and p.suffix == ".json" else removed).append(size)
        if not (recent and p.suffix == ".json"):
            p.unlink()
    return kept, removed


def remove_empty_folders(root):
    for d in sorted((p for p in root.rglob("*") if p.is_dir()), key=lambda p: len(p.parts), reverse=True):
        if not any(d.iterdir()):
            d.rmdir()


def mib(sizes):
    return f"{sum(sizes) / 2**20:.1f} MiB"


def set_ci_output(name, value):
    """A step output for the workflow (steps.<id>.outputs.<name>), when CI runs this."""
    if "GITHUB_OUTPUT" in os.environ:
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as out:
            out.write(f"{name}={value}\n")


def cmd_prune_build_cache(args):
    """Before CI saves BUILD_CACHE_ENV: keep only the objects and clang-tidy results this run used, and tell the save
    step (`save`) whether anything is left: an empty folder saved as the newest entry would make the next run cold."""
    root = build_cache_root()
    objects, tidy = ([], []), ([], [])
    if root and root.is_dir():
        for folder in sorted(p for p in root.iterdir() if p.is_dir()):
            if folder.name == "tidy":
                tidy = prune_tidy(folder)
            else:
                kept, removed = prune_objects(folder, every=cold_ref())
                objects = (objects[0] + kept, objects[1] + removed)
        remove_empty_folders(root)
    save = bool(root and root.is_dir() and any(root.iterdir()))
    print(f"prune-build-cache: kept {len(objects[0])} object cache files ({mib(objects[0])}) and {len(tidy[0])} clang-tidy results "
          f"({mib(tidy[0])}), removed {len(objects[1])} ({mib(objects[1])}) and {len(tidy[1])} ({mib(tidy[1])})"
          + ("" if save else "; nothing to save"))
    set_ci_output("save", "true" if save else "false")


def packages_dir():
    """XMake keys its package cache on the toolchain name, not the compiler version, so each
    environment gets its own: a toolchain bump never reuses stale builds. pixi.toml counts too,
    because activation variables (build flags, pkg-config paths) aren't in the lock."""
    return Path.home() / ".xmake" / "project_n" / sha256("pixi.toml", "pixi.lock")[:12] / "packages"


def xmake_env(bdir):
    # Per-build config dir: native and container builds of one preset never share XMake state.
    # The user's own sanitizer options go last: the last value of an option wins.
    sanitizer_env = {k: f"{v}:{os.environ[k]}" if os.environ.get(k) else v for k, v in SANITIZER_ENV.items()}
    # CL_WRAPPER drops /sourceDependencies only when asked: xmake.lua's build cache is off for it here.
    return dict(os.environ, XMAKE_CONFIGDIR=str(ROOT / bdir / ".xmake"), XMAKE_PKG_INSTALLDIR=str(packages_dir()),
                PROJECT_N_CL_DROP_SOURCE_DEPENDENCIES="1", **sanitizer_env)


def check_sanitizer(preset, sanitizer):
    if sanitizer != "none" and sanitizer not in SANITIZERS[preset]:
        raise Fail(f"{preset} supports sanitizers: {', '.join(SANITIZERS[preset])}")


def pinned_xmake():
    m = XMAKEVER_RE.search((ROOT / "xmake.lua").read_text(encoding="utf-8"))
    if not m:
        raise Fail("set_xmakever(...) not found in xmake.lua")
    return m[1]


def read_version():
    m = VERSION_RE.search((ROOT / "xmake.lua").read_text(encoding="utf-8"))
    if not m:
        raise Fail("set_version(...) not found in xmake.lua")
    return m[1]


def with_version(xmake_lua, version):
    return VERSION_RE.sub(f'set_version("{version}")', xmake_lua, count=1)


def image_tag(name):
    spec = IMAGES[name]
    h = hashlib.sha256(sha256(spec["file"], *IMAGE_INPUTS).encode())
    if spec["base"]:
        h.update(image_tag(spec["base"]).encode())
    return f"localhost/project_n-{name}:{h.hexdigest()[:12]}"


def ensure_image(name):
    tag = image_tag(name)
    if subprocess.run(["podman", "image", "exists", tag], check=False).returncode == 0:
        return tag
    spec = IMAGES[name]
    registry = os.environ.get("PROJECT_N_REGISTRY")
    remote = f"{registry}/{tag.removeprefix('localhost/')}" if registry and spec["push"] else None
    if remote and subprocess.run(["podman", "pull", "-q", remote], check=False).returncode == 0:
        run(["podman", "tag", remote, tag])
        return tag
    cmd = ["podman", "build", "-t", tag, "-f", spec["file"], "--ignorefile", IMAGE_IGNORE]
    if spec["base"]:
        cmd += ["--build-arg", f"BASE_IMAGE={ensure_image(spec['base'])}"]
    run(cmd + ["."])
    if remote and os.environ.get("PROJECT_N_PUSH_IMAGES") == "1":
        run(["podman", "push", tag, remote])
    return tag


def msvc_dir():
    """MSVC and the Windows SDK, installed once per msvc.Containerfile (which pins their versions)."""
    return Path.home() / ".xmake" / "project_n-tools" / f"msvc-{sha256(IMAGES['msvc']['file'])[:12]}"


def ensure_msvc(image):
    dest = msvc_dir()
    if not (dest / ".installed").exists():
        if os.environ.get("PROJECT_N_ACCEPT_MSVC_LICENSE") != "1":
            raise Fail("Installing MSVC downloads Microsoft's Build Tools. Accept their licence by setting PROJECT_N_ACCEPT_MSVC_LICENSE=1.")
        shutil.rmtree(dest, ignore_errors=True)
        dest.mkdir(parents=True)
        run(container_cmd(image, ["bash", "-c", MSVC_INSTALL], mounts=[f"{dest}:/opt/msvc:Z"]))
        (dest / ".installed").touch()
    install_cl_wrapper(dest)
    # The install also holds an empty servicing stub of Microsoft's newest toolset (14.51), and
    # XMake (--vs_toolset is Windows-only) takes the newest folder as the toolset. Keep real ones.
    for toolset in (dest / "VC" / "Tools" / "MSVC").glob("*"):
        if not (toolset / "bin").is_dir():
            shutil.rmtree(toolset)
    return dest


def install_cl_wrapper(dest):
    """Put CL_WRAPPER at `dest`/bin/x64/cl.exe in place of msvc-wine's symlink; rewritten only when it differs."""
    wrapper = dest / "bin" / "x64" / "cl.exe"
    if wrapper.is_symlink() or not wrapper.exists() or wrapper.read_text(encoding="utf-8") != CL_WRAPPER:
        wrapper.unlink(missing_ok=True)
        wrapper.write_text(CL_WRAPPER, encoding="utf-8", newline="\n")
    wrapper.chmod(0o755)


def preset_container(preset):
    """The preset's image and mounts: windows-msvc also gets the MSVC install, never part of an image."""
    if not PRESETS[preset]["image"]:
        raise Fail(f"{preset} builds only on a {PRESETS[preset]['host']} host; CI's {PRESETS[preset]['host']} job covers it")
    image = ensure_image(PRESETS[preset]["image"])
    return image, [f"{ensure_msvc(image)}:/opt/msvc:Z"] if PRESETS[preset]["toolchain"] == "msvc" else []


def container_cmd(image, inner, interactive=False, mounts=()):
    cache = os.environ.get("PROJECT_N_XMAKE_CACHE")
    if cache:
        cache = Path(cache).absolute()  # podman takes a relative source for a volume name
        cache.mkdir(parents=True, exist_ok=True)
    cmd = ["podman", "run", "--rm", "--userns=keep-id:uid=1000,gid=1000", "-v", f"{ROOT}:/src:Z",
           "-v", f"{cache}:/home/dev/.xmake:Z" if cache else f"{CACHE_VOLUME}:/home/dev/.xmake", "-e", f"PROJECT_N_IMAGE={image}"]
    objects = build_cache_root()
    if objects:
        objects.mkdir(parents=True, exist_ok=True)
        cmd += ["-v", f"{objects}:{CONTAINER_BUILD_CACHE}:Z", "-e", f"{BUILD_CACHE_ENV}={CONTAINER_BUILD_CACHE}"]
    if os.environ.get("GITHUB_REF"):  # the cold rule holds inside too
        cmd += ["-e", f"GITHUB_REF={os.environ['GITHUB_REF']}"]
    for mount in mounts:
        cmd += ["-v", mount]
    if interactive:
        cmd.append("-it")
    return cmd + [image, *inner]


def reenter(preset, argv, interactive=False):
    """Run `project_n <argv>` inside the preset's container: one code path, another environment."""
    image, mounts = preset_container(preset)
    run(container_cmd(image, ["pixi", "run", "--quiet", "project_n", *argv], interactive, mounts))


def configure(preset, mode, sanitizer="none", unity=False):
    check_sanitizer(preset, sanitizer)
    p, bdir = PRESETS[preset], build_dir(preset, mode, sanitizer, unity)
    mark_used(ROOT / bdir, time.time())
    # -v makes XMake install packages one at a time (else 4 in parallel). XMake installs
    # duplicate variants with the same build hash (libxcb, libx11 for SDL3) concurrently into one
    # directory and they corrupt each other; drop -v once that's fixed upstream.
    cmd = ["xmake", "f", "-y", "-v", "-p", p["plat"], "-a", p["arch"], f"--toolchain={p['toolchain']}", "-m", mode,
           "-o", bdir, f"--sanitizer={sanitizer}", *(["--unity_build=y"] if unity else [])]
    if p["toolchain"] == "msvc":
        cmd.append("--sdk=/opt/msvc" if IN_CONTAINER else f"--vs_toolset={VS_TOOLSET}")
    if cold_ref() and holds_objects(bdir):
        raise Fail(f"{bdir} already holds compiled objects, and CI builds {os.environ['GITHUB_REF']} from source: "
                   "delete the folder first (the cold rule, tools/AGENTS.md)")
    cache = object_cache_dir(preset, mode, sanitizer, unity)
    # On main a build only writes, into an empty folder: it reads no object an earlier build of the run made.
    if cache and not (cold_ref() and cache.is_dir() and any(cache.iterdir())):
        cache.mkdir(parents=True, exist_ok=True)
        cmd += ["--build_cache=y", f"--ccachedir={cache}"]  # the option beats XMake's own switch, off on CI
    elif cold_ref():
        cmd.append("--ccache=n")  # nor XMake's own cache in the build folder, on in containers (no CI variable there)
    run(cmd, env=xmake_env(bdir))
    return bdir


def build(preset, mode, sanitizer="none", unity=False):
    bdir = configure(preset, mode, sanitizer, unity)
    run(["xmake", "build"], env=xmake_env(bdir))
    if not IN_CONTAINER and sanitizer == "none" and not unity:  # unity's generated sources would confuse clangd
        # Native builds keep clangd current (.clangd reads build/; container paths would be wrong).
        run([*COMPDB, ROOT / "build"], env=xmake_env(bdir))
    return bdir


def binary_path(preset, mode, name=APP):
    p = PRESETS[preset]
    exe = name + (".exe" if p["plat"] == "windows" else "")
    return build_dir(preset, mode) / p["plat"] / p["arch"] / mode / exe


def cmd_build(args):
    sweep_here(own_trees(list(PRESETS) if args.preset == "all" else [args.preset], args.mode, args.sanitizer,
                         container=args.container))
    if args.preset == "all":
        return build_all(args)
    check_sanitizer(args.preset, args.sanitizer)
    if needs_container(args.preset, args.container):
        return reenter(args.preset, ["build", "--preset", args.preset, "--mode", args.mode, "--sanitizer", args.sanitizer])
    build(args.preset, args.mode, args.sanitizer)


def build_all(args):
    """Build every preset in turn, past failures, then summarise; each build's errors print as it runs."""
    results = {}
    for preset in PRESETS:
        if not buildable_here(preset):
            results[preset] = f"skipped (needs a {PRESETS[preset]['host']} host)"
            continue
        try:
            cmd_build(argparse.Namespace(**{**vars(args), "preset": preset}))
            results[preset] = "ok"
        except Fail as e:
            results[preset] = f"FAILED ({e})"
        except subprocess.CalledProcessError:
            results[preset] = "FAILED (errors above)"
    print("\nbuild summary:")
    for preset, result in results.items():
        print(f"  {preset:14} {result}")
    return 1 if any(r.startswith("FAILED") for r in results.values()) else 0


def test_names():
    """What `test --suite` and `--test` take, for shell completion (--help and --list-tests): each
    target tests/xmake.lua registers, with its test files without their `_tests` suffix."""
    suites = {}
    # Reads only one-line unit_tests(...) calls; a multi-line one drops out of --test, so widen the regex then.
    for name, files in re.findall(r'^unit_tests\("(\w+)", (\{[^}]*\}|"[^"]*")', (ROOT / "tests" / "xmake.lua").read_text(), re.MULTILINE):
        paths = (f for pattern in re.findall(r'"([^"]+)"', files) for f in (ROOT / "tests").glob(pattern))
        suites[name] = sorted(f.stem.removesuffix("_tests") for f in paths if f.stem.endswith("_tests"))
    return suites


def all_test_files(suites):
    return sorted({n for names in suites.values() for n in names})


def test_file_pattern(stem):
    """doctest's `--source-file` value for the cases of `<stem>_tests.cpp`, as tests/xmake.lua's `unit_tests` registers
    one test per file: doctest matches the whole `__FILE__` the compiler saw (`tests/unit/core/log_tests.cpp`, or with
    `\\` from MSVC), so the separator before the name keeps `log` from taking `sdl_log_tests.cpp`. doctest splits on
    commas and keeps a backslash before anything but a comma or a backslash. Mirrored in tests/xmake.lua."""
    return f"*/{stem}_tests.cpp,*\\{stem}_tests.cpp"


def test_filter(name, suite):
    """doctest's filter for `test --suite SUITE --test NAME`: the cases of the test file NAME names, else the cases with
    NAME in their name. A file of another suite fails: doctest passes a run whose filter selects nothing."""
    suites = test_names()
    if name not in all_test_files(suites):
        return f"--test-case=*{name}*"
    if name not in suites.get(suite, ()):
        raise Fail(f"{name}_tests.cpp is in {', '.join(s for s, files in suites.items() if name in files)}, not {suite}")
    # The whole file name: `*log*` would also match logic_tests.cpp, and `*game*` every file under unit/game/.
    return f"--source-file={test_file_pattern(name)}"


def cmd_test(args):
    if getattr(args, "list_tests", False):  # internal callers build their own Namespace
        suites = test_names()
        print("\n".join(suites.get(args.target, ()) if args.target else all_test_files(suites)))
        return
    extra = args.args[1:] if args.args[:1] == ["--"] else args.args
    if (extra or args.test) and not args.target:
        raise Fail("pass --suite <test target> with --test or doctest arguments")
    extra = ([test_filter(args.test, args.target)] if args.test else []) + extra
    check_sanitizer(args.preset, args.sanitizer)
    sweep_here(own_trees([args.preset], args.mode, args.sanitizer, args.unity, args.container))
    if needs_container(args.preset, args.container):
        argv = ["test", "--preset", args.preset, "--mode", args.mode, "--sanitizer", args.sanitizer, *(["--unity"] if args.unity else [])]
        argv += (["--target", args.target] if args.target else []) + (["--", *extra] if extra else [])
        return reenter(args.preset, argv)
    bdir = build(args.preset, args.mode, args.sanitizer, args.unity)
    if PRESETS[args.preset]["plat"] == "windows" and args.sanitizer != "none" and HOST != "windows":
        # MSVC's ASan hot-patches system DLLs; Wine's (CET endbr64 prologues) can't be patched.
        print(f"test: built with {args.sanitizer} sanitizer; MSVC sanitizer runs need native Windows, not Wine")
        return
    if args.target in test_names() and not extra:
        # Each test file is a test of its own (tests/xmake.lua), so a suite's files run in parallel.
        run(["xmake", "test", "--output-on-failure", f"{args.target}/*"], env=xmake_env(bdir))
    elif args.target:
        run(["xmake", "run", args.target, *extra], env=xmake_env(bdir))
    else:
        run(["xmake", "test", "--output-on-failure"], env=xmake_env(bdir))


def pixi_prefix(name):
    """Install one of pixi.toml's environments and return its prefix. Pixi itself names it: the
    lavapipe environment holds only Mesa and the loader, no printenv."""
    pixi = os.environ.get("PIXI_EXE", "pixi")
    run([pixi, "install", "--locked", "-e", name])
    envs = json.loads(capture([pixi, "info", "--json"]))["environments_info"]
    return Path(next(e["prefix"] for e in envs if e["name"] == name))


DOCTEST_COUNT_RE = re.compile(r"unskipped test cases passing the current filters: (\d+)")


def reference_case_count(exe, env):
    """How many test cases the reference binary holds, from doctest's `--count`."""
    result = subprocess.run([exe, "--count"], cwd=ROOT, env=env, check=False, capture_output=True, encoding="utf-8",
                            errors="replace")
    m = DOCTEST_COUNT_RE.search(result.stdout)
    if not m:
        raise Fail(f"reference: {exe} --count printed no case count:\n{result.stdout}{result.stderr or ''}")
    return int(m[1])


def doctest_case_name(output):
    """The test case a doctest failure names, if it printed one."""
    m = re.search(r"^TEST CASE:\s+(.+)$", output, re.MULTILINE)
    return m and m[1].strip()


def run_reference_case(exe, env, index):
    """(output, return code) of the reference binary's `index`th case (1-based, doctest's order), stdout and stderr
    together. Cases are picked by position: some names hold commas, which --test-case would split on. --minimal drops
    doctest's summary from a passing case: it would claim every case passed, counting those outside the range."""
    # Its own runtime directory: without one every case makes Mesa create /tmp/xdg-runtime-mesa-<uid> at once, and the
    # loser crashes.
    with tempfile.TemporaryDirectory(prefix="project_n-xdg-") as runtime_dir:
        result = subprocess.run([exe, f"--first={index}", f"--last={index}", "--minimal=true"], cwd=ROOT,
                                env={**env, "XDG_RUNTIME_DIR": runtime_dir}, check=False, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, encoding="utf-8", errors="replace")
    return result.stdout, result.returncode


def run_references(preset, update=False, quiet=False):
    """Compare a built release's pictures of fixed scenes with tests/reference/, on Mesa's software
    Vulkan (Pixi's lavapipe environment) and without a display: every machine draws the same. Returns every case's
    output (nothing with `update`), which `measure` reads the world's shimmer from; `quiet` prints it only when a case
    fails."""
    lavapipe = pixi_prefix("lavapipe")
    env = {**os.environ, "SDL_VIDEO_DRIVER": "offscreen", "SDL_VULKAN_LIBRARY": str(lavapipe / "lib" / "libvulkan.so.1"),
           "VK_DRIVER_FILES": str(lavapipe / "share" / "vulkan" / "icd.d" / "lvp_icd.x86_64.json")}
    drawn = ROOT / "build" / "reference"
    shutil.rmtree(drawn, ignore_errors=True)
    drawn.mkdir(parents=True)
    exe = binary_path(preset, "release", "reference_tests")
    count = reference_case_count(exe, env)
    # One process per case, all cores at once: a single process kept lavapipe on 1.4 of 32 cores (28 s for 25 cases).
    print(f"+ {exe} on {count} case(s), one process each, {jobs()} at a time", flush=True)
    with concurrent.futures.ThreadPoolExecutor(jobs()) as pool:
        results = list(pool.map(lambda index: run_reference_case(exe, env, index), range(1, count + 1)))
    failed = [doctest_case_name(output) or f"case {index}" for index, (output, returncode) in enumerate(results, 1)
              if returncode]
    if not quiet or failed:
        for output, _ in results:
            print(output, end="", flush=True)
    if failed and not update:
        raise subprocess.CalledProcessError(1, [str(exe)], stderr=f"reference: {len(failed)} of {count} case(s) failed: "
                                                                 + "; ".join(failed) + "\n")
    if update:
        # Every committed view must be redrawn: a run that crashed part-way would update only some.
        missing = sorted({p.name for p in (ROOT / "tests" / "reference").glob("*.png")} - {p.name for p in drawn.glob("*.png")})
        if missing:
            raise Fail(f"reference: nothing updated; the run didn't draw {', '.join(missing)}")
        for picture in drawn.glob("*.png"):
            shutil.copy(picture, ROOT / "tests" / "reference" / picture.name)
        print("reference: tests/reference/ updated; look at the pictures before committing them")
        return
    # Lavapipe draws every case the same to the byte: a passing picture that differs has drifted within the tolerance.
    changed = [p.stem for p in sorted(drawn.glob("*.png"))
               if (ROOT / "tests" / "reference" / p.name).is_file()
               and p.read_bytes() != (ROOT / "tests" / "reference" / p.name).read_bytes()]
    if changed:
        print(f"reference: within tolerance but changed: {', '.join(changed)}; "
              "refresh them with `pixi run project_n reference --update`")
    return "".join(output for output, _ in results)


def cmd_reference(args):
    if PRESETS[args.preset]["plat"] != "linux":
        raise Fail("`reference` runs on Linux only (lavapipe here is linux-64's): pass --preset linux-gcc or linux-clang")
    if needs_container(args.preset, args.container):
        return reenter(args.preset, ["reference", "--preset", args.preset, *(["--update"] if args.update else [])])
    build(args.preset, "release")
    run_references(args.preset, args.update)


def cmd_run(args):
    if PRESETS[args.preset]["host"] != HOST:
        raise Fail(f"`run` is native only; {args.preset} needs a {PRESETS[args.preset]['host']} host")
    bdir = build(args.preset, args.mode, args.sanitizer)
    extra = args.args[1:] if args.args[:1] == ["--"] else args.args
    run(["xmake", "run", *(["-d"] if args.debug else []), APP, *extra], env=xmake_env(bdir))


def is_source(path):
    """Our C++ sources: what fmt and lint cover. Headers in a generated/ folder are written by `shaders` and `atlas`."""
    return path.startswith(("src/", "tests/")) and path.endswith((".cpp", ".hpp")) and "/generated/" not in path


def source_files():
    # Forward slashes on every host, as git prints them: fmt and lint match these against git's paths.
    return sorted(f for f in (p.relative_to(ROOT).as_posix() for d in ("src", "tests") for p in (ROOT / d).rglob("*")) if is_source(f))


# Findings are fixed properly first; a suppression is the last resort (root AGENTS.md). One that
# remains must be narrow and explained: NOLINT names its checks and gives a reason
# (`// NOLINT(check-name): why`), a diagnostic pragma carries a comment, a nested .clang-tidy
# says why in a `#` comment.
NOLINT_RE = re.compile(r"NOLINT(?P<kind>NEXTLINE|BEGIN|END)?(?:\((?P<checks>[^)]*)\))?(?P<rest>.*)")
PRAGMA_RE = re.compile(r"#\s*pragma\s+(?:(?:clang|GCC)\s+diagnostic\s+ignored|warning\s*\(\s*disable)")


def nolint_explained(line):
    m = NOLINT_RE.search(line)
    return not m or m["kind"] == "END" or bool(m["checks"] and re.match(r"\s*:\s*\S", m["rest"]))


def unexplained_suppressions(files):
    found = []
    for f in files:
        lines = (ROOT / f).read_text(encoding="utf-8").splitlines()
        for n, line in enumerate(lines, 1):
            if not nolint_explained(line):
                found.append(f"{f}:{n}: name the check(s) and the reason: // NOLINT(check-name): why")
            previous = lines[n - 2].strip() if n > 1 else ""
            if PRAGMA_RE.search(line) and not (re.search(r"//\s*\S", line) or previous.startswith("//")):
                found.append(f"{f}:{n}: explain the pragma in a comment on it or the line above")
    for d in ("src", "tests"):
        for p in (ROOT / d).rglob(".clang-tidy"):
            if not any(line.lstrip().startswith("#") for line in p.read_text(encoding="utf-8").splitlines()):
                found.append(f"{p.relative_to(ROOT).as_posix()}: explain in a # comment why these checks can't be met")
    return found


# Code comments explain why in their own words; they never point into the design process. Documentation
# (*.md, docs/) keeps its references. Each pattern needs a digit or a section mark, so ordinary prose
# ("the third phase", "a chunk of the file") never matches; "P<digit>" is left out (display P3 is a real name).
DESIGN_REFERENCE_PATTERNS = (
    r"\b(?:phase|chunk)s?\s+\d",
    r"\bthe\s+spec\b|\bspec's\s+(?:outcome|history)\b|\b(?:chunk|phase|design)\s+(?:\d\w*\s+)?spec\b",
    r"\bdesign\s+(?:docs?|documents?)\b",
    r"\b000\d(?:\.\d+)*\b",
    "\u00a7",
    r"\b(?:[3-9]|1[01])[a-c]\d?(?:-look)?\b",
    r"\bruling\b",
    r"\bR\d[a-z]\b",
    r"\btasks?\s+\d",
    r"(?-i:\bwaves?\s+[A-J]\b)",
    r"\bthe\s+(?:plan|ledger|brainstorm)\b",
    r"\b20\d\d-\d\d-\d\d\b",
    r"\bthe\s+user's\s+(?!own\b)",
)
DESIGN_REFERENCE_RE = re.compile("|".join(DESIGN_REFERENCE_PATTERNS), re.IGNORECASE)
DESIGN_REFERENCE_MESSAGE = ("code comments don't cite specs, chunks, phases, plans, rulings, tasks or design docs: "
                            "say why in your own words")
C_COMMENT_SUFFIXES = (".cpp", ".hpp", ".hlsl", ".hlsli", ".dox")
HASH_COMMENT_SUFFIXES = (".py", ".yaml", ".yml", ".toml", ".sh", ".Containerfile")
HASH_COMMENT_NAMES = ("Containerfile", "gdbinit", "pre-commit", "commit-msg", "pre-push", ".editorconfig", ".gitignore",
                      ".gitattributes", ".containerignore", ".clang-format", ".clang-tidy", ".clangd")
UNCHECKED_PREFIXES = ("docs/", "assets/sprites/", "assets/fonts/", "assets/manifest.yaml", "tests/fuzz/corpus/",
                      "tests/reference/", "shaders/bin/", ".claude/")


def comment_kind(path):
    """How `path`'s comments are written: "c", "lua", "hash", or None for documentation, generated and data files."""
    name = Path(path).name
    if path.startswith(UNCHECKED_PREFIXES) or "/generated/" in path or name.endswith((".lock", ".md")):
        return None
    if path.endswith(C_COMMENT_SUFFIXES) or path.startswith(".vscode/") and path.endswith(".json"):
        return "c"
    if path.endswith(".lua"):
        return "lua"
    if path.endswith(HASH_COMMENT_SUFFIXES) or name in HASH_COMMENT_NAMES or "Containerfile" in name:
        return "hash"
    return None


def design_reference_files(files):
    return [f for f in files if comment_kind(f)]


def c_comments(text, line_marker, block):
    """(line, text) of each physical line of each comment in C-like source: `//` or `--` lines and block comments,
    skipping string and character literals, C++ raw strings and digit separators."""
    out, i, line, n = [], 0, 1, len(text)
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
        elif text.startswith(line_marker, i) and not (block and text.startswith(block[0], i)):
            end = text.find("\n", i)
            end = n if end < 0 else end
            out.append((line, text[i:end]))
            i = end
            continue
        elif block and text.startswith(block[0], i):
            end = text.find(block[1], i + len(block[0]))
            end = n if end < 0 else end + len(block[1])
            for k, part in enumerate(text[i:end].split("\n")):
                out.append((line + k, part))
            line += text.count("\n", i, end)
            i = end
            continue
        elif c == '"' and line_marker == "//" and text[max(0, i - 1)] == "R":
            m = re.compile(r'"([^()\\ ]{0,16})\(').match(text, i)
            end = text.find(")" + m[1] + '"', i) if m else -1
            if end >= 0:
                end += len(m[1]) + 2
                line += text.count("\n", i, end)
                i = end
                continue
        elif c == '"' or (c == "'" and not text[max(0, i - 1)].isalnum()):
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            i = j
        i += 1
    return out


def hash_comments(text):
    """(line, text) of each `#` comment: one that starts a line or follows whitespace, outside quotes."""
    out = []
    for line, row in enumerate(text.splitlines(), 1):
        quote = ""
        for i, c in enumerate(row):
            if quote:
                quote = "" if c == quote else quote
            elif c in "\"'":
                quote = c
            elif c == "#" and (i == 0 or row[i - 1].isspace()):
                out.append((line, row[i:]))
                break
    return out


def python_comments(text):
    """(line, text) of each comment and each docstring line of Python source."""
    out = [(t.start[0], t.string) for t in tokenize.generate_tokens(io.StringIO(text).readline) if t.type == tokenize.COMMENT]
    for node in ast.walk(ast.parse(text)):
        if isinstance(node, ast.Expr) and isinstance(node.value, ast.Constant) and isinstance(node.value.value, str):
            out += [(node.lineno + k, part) for k, part in enumerate(ast.get_source_segment(text, node).split("\n"))]
    return out


def design_references(path, text):
    """`path:line: message` for each comment line of `text` that cites the design process."""
    kind = comment_kind(path)
    if not kind:
        return []
    if kind == "c":
        comments = c_comments(text, "//", ("/*", "*/"))
    elif kind == "lua":
        comments = c_comments(text, "--", None)
    elif path.endswith(".py"):
        comments = python_comments(text)
    else:
        comments = hash_comments(text)
    return [f"{path}:{line}: {DESIGN_REFERENCE_MESSAGE}" for line, body in sorted(set(comments))
            if DESIGN_REFERENCE_RE.search(body)]


def design_reference_findings(files=None):
    """Findings in the given files, or in every tracked and unignored file of the repository."""
    if files is None:
        files = capture(["git", "ls-files", "--cached", "--others", "--exclude-standard"]).splitlines()
    found = []
    for f in design_reference_files(sorted(set(files))):
        if (ROOT / f).is_file():
            found += design_references(f, (ROOT / f).read_text(encoding="utf-8"))
    return found


def widens_scope(path):
    """A change here can change any file's format or lint result: the tools' config (at any depth),
    the build (flags, packages), the tool versions, or lint's own rules in this file."""
    return (Path(path).name in (".clang-format", ".clang-tidy", ".clangd", "xmake.lua")
            or path in ("pixi.lock", "tools/project_n.py", "docs/Doxyfile") or path.startswith("xmake/repo/"))


def scope_range():
    """fmt and lint's default diff: every commit of a branch since it left main, pushed or not (a
    pushed commit isn't a checked one), or main's unpushed commits."""
    if capture(["git", "rev-parse", "--abbrev-ref", "HEAD"]).strip() == "main":
        return commit_range()
    has_main = capture(["git", "rev-parse", "--verify", "--quiet", "refs/heads/main"], check=False).strip()
    return "main..HEAD" if has_main else "origin/main..HEAD"  # CI's clones have no local main


def changed_files(rng):
    """Files changed since the range's base (none: the working tree only), plus uncommitted and new ones."""
    base = capture(["git", "merge-base", *rng.split("..")], check=False).strip() if rng else "HEAD"
    if not base:
        return None  # the base isn't in this clone (a force-pushed CI range): cover everything
    # Deleted and renamed-away paths count: deleting a nested .clang-tidy changes other files' results.
    return set(capture(["git", "diff", "--name-only", "--no-renames", base]).splitlines()
               + capture(["git", "ls-files", "--others", "--exclude-standard"]).splitlines())


def scoped_sources(args):
    """(sources, changed files) for fmt and lint: the diff against scope_range(), or with --all or a
    change that widens_scope() every source and None."""
    changed = None if args.all else changed_files(args.range or scope_range())
    if changed is None or any(widens_scope(f) for f in changed):
        return source_files(), None
    return [f for f in source_files() if f in changed], changed


INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')
# The only src/ headers that may include a library: one facade each, which all code goes through.
FACADES = {"src/core/log.hpp": "quill/", "src/core/profile.hpp": "tracy/", "src/core/ecs.hpp": "entt/", "src/core/jobs.hpp": "enkiTS/"}
# Facades whose header hides the library behind a pimpl: only their .cpp includes it.
HIDDEN_FACADES = {"src/core/jobs.hpp"}
# The src/ modules and the modules each one's code may include: those below it.
MODULES = {"core": (), "data": ("core",), "platform": ("core",), "gfx": ("core", "platform"),
           "ui": ("core", "gfx", "platform"), "audio": ("core",), "game": ("core",),
           "app": ("core", "data", "platform", "gfx", "ui", "audio", "game")}


def is_standard(bracket, name):
    """Standard headers are the only bracketed names with neither a directory nor an extension."""
    return bracket == "<" and bool(re.fullmatch(r"[a-z0-9_]+", name))


def standard_includes_in_sources(sources):
    """Standard headers that a .cpp among these includes itself: each target's precompiled header
    provides them (src/pch.hpp, tests/unit/pch.hpp). Headers still include what they use."""
    found = []
    for f in [f for f in sources if f.startswith(("src/", "tests/")) and f.endswith(".cpp")]:
        for n, line in enumerate((ROOT / f).read_text(encoding="utf-8").splitlines(), 1):
            m = INCLUDE_RE.match(line)
            if m and is_standard(*m.groups()):
                found.append(f"{f}:{n}: {line.strip()}")
    return found


# Built-in number types and the std:: fixed-width names; core/types.hpp's i32, u8, f32... replace them.
UNSIZED_RE = re.compile(r"\b(?:unsigned|signed|short|long|int|float|double)\b|\b(?:std::)?u?int(?:8|16|32|64)_t\b")


def unsized_types(sources):
    """Lines in src/ and tests/ that name a number type other than core/types.hpp's sized ones.
    `int main(int, char**)` is exempt: the standard fixes that signature."""
    found = []
    for f in [f for f in sources if f.startswith(("src/", "tests/")) and f.endswith((".cpp", ".hpp")) and f != "src/core/types.hpp"]:
        for n, line in enumerate((ROOT / f).read_text(encoding="utf-8").splitlines(), 1):
            code = code_of(line)
            if UNSIZED_RE.search(code) and not re.match(r"\s*int main\(", code):
                found.append(f"{f}:{n}: {line.strip()}")
    return found


def leaked_dependencies(sources):
    """Includes in the src/ headers among these that are neither the standard library nor ours:
    third-party and platform headers belong in .cpp files, behind our own types. A facade header
    (FACADES, but not HIDDEN_FACADES) may include its own library, and a module's internal/ headers its libraries."""
    found = []
    for header in [f for f in sources if f.startswith("src/") and f.endswith(".hpp")]:
        for n, line in enumerate((ROOT / header).read_text(encoding="utf-8").splitlines(), 1):
            m = INCLUDE_RE.match(line)
            if not m:
                continue
            bracket, name = m.groups()
            standard = is_standard(bracket, name)
            ours = bracket == '"' and (((ROOT / header).parent / name).is_file() or (ROOT / "src" / name).is_file())
            facade = header in FACADES and header not in HIDDEN_FACADES and name.startswith(FACADES[header])
            internal = "/internal/" in header  # a module's private headers may include its libraries
            if not (standard or ours or facade or internal):
                found.append(f"{header}:{n}: {line.strip()}")
    return found


def crossed_modules(sources):
    """Includes in src/ and tests/ among these that go against the module direction (MODULES): a
    module's code includes only its own headers and those of the modules below it, and a module's
    internal/ headers only from inside it. main.cpp and the tests may include any public header."""
    found = []
    for f in [f for f in sources if f.startswith(("src/", "tests/")) and f.endswith((".cpp", ".hpp"))]:
        parts = f.split("/")
        own = parts[1] if parts[0] == "src" and len(parts) > 2 else None
        for n, line in enumerate((ROOT / f).read_text(encoding="utf-8").splitlines(), 1):
            m = INCLUDE_RE.match(line)
            if not m or m[1] != '"':
                continue
            module, _, rest = m[2].partition("/")
            if module not in MODULES or module == own:
                continue
            if rest.startswith("internal/") or (own and module not in MODULES.get(own, ())):
                found.append(f"{f}:{n}: {line.strip()}")
    return found


# The platform maths library's transcendental functions, whose results differ between machines.
PLATFORM_MATHS_RE = re.compile(r"(?:\bstd::|(?<![\w:])::|(?<![\w.:>]))(?:a?sinh?|a?cosh?|a?tanh?|atan2|exp2?|expm1|log(?:2|10|1p)?|pow|cbrt|hypot|erfc?|[lt]gamma)[fl]?\s*\(")


def platform_maths(sources):
    """Lines in src/core/ and src/game/ that call the platform's transcendental functions: the
    simulation uses core/math.hpp's, which give the same results on every machine."""
    found = []
    for f in [f for f in sources if f.startswith(("src/core/", "src/game/")) and f.endswith((".cpp", ".hpp"))]:
        for n, line in enumerate((ROOT / f).read_text(encoding="utf-8").splitlines(), 1):
            if PLATFORM_MATHS_RE.search(code_of(line)):
                found.append(f"{f}:{n}: {line.strip()}")
    return found


# Output that goes around core/log.hpp, searched in code_of() a line.
OUTPUT_RE = re.compile(r"\bstd::(?:print|println|cout|cerr|clog)\b|\b(?:printf|fprintf|puts|SDL_Log\w*)\s*\(")


def code_of(line):
    """A source line without its string literals' contents and its // comment."""
    return re.sub(r'"(?:\\.|[^"\\])*"', '""', line).split("//")[0]


def bypassed_facades(sources):
    """Lines in src/ that go around a facade: all output goes through PN_LOG_*, and Quill, Tracy,
    EnTT and enkiTS are included only by their facade (FACADES) and its .cpp, so each library is
    used the same way everywhere."""
    found = []
    for f in [f for f in sources if f.startswith("src/") and f.endswith((".cpp", ".hpp"))]:
        own = FACADES.get(f.rsplit(".", 1)[0] + ".hpp")  # a facade's header and .cpp include its library
        for n, line in enumerate((ROOT / f).read_text(encoding="utf-8").splitlines(), 1):
            m = INCLUDE_RE.match(line)
            library = m and next((lib for lib in FACADES.values() if m[2].startswith(lib)), None)
            if (library and library != own) or OUTPUT_RE.search(code_of(line)):
                found.append(f"{f}:{n}: {line.strip()}")
    return found


# A clang-tidy finding's first line: "<path>:<line>:<col>: error: <message> [<check>]".
TIDY_FINDING_RE = re.compile(r"^(.+?):\d+:\d+: (?:error|warning): .*\[([^\]]+)\]$", re.MULTILINE)


# clang-tidy findings located in library code that lint ignores, as (path suffix, check): each
# can't be fixed in our code. Any other finding fails, wherever it is.
IGNORED_FINDINGS = {
    # A false positive inside Quill, reached through every PN_LOG_* call.
    ("quill/core/InlinedVector.h", "clang-analyzer-security.ArrayBound"),
}


def listed_finding(path, checks):
    """Whether a finding at `path` with clang-tidy's `checks` label is one IGNORED_FINDINGS lists."""
    return any(path.endswith(listed) and checks.split(",")[0] == check for listed, check in IGNORED_FINDINGS)


def finding_key(text, path):
    """`text` with the finding's `path` resolved: clang-tidy spells one header `./src/…`, `src/…` or absolutely,
    depending on the unit that reached it."""
    return text.replace(path, os.path.normpath(os.path.join(ROOT, path))) if path else text


def jobs():
    """How many processes to run at once: the CPUs this process may use (affinity, cgroup cpusets), not the machine's."""
    return os.process_cpu_count() or 1


def tidy_verdict(output, returncode):
    """Whether a clang-tidy run fails lint, and how many listed findings (IGNORED_FINDINGS) it
    ignored. clang-tidy keeps an analyzer finding located in a library when its path starts in our
    code: usually our misuse, which fails; only the listed ones don't. A crash always fails."""
    findings = TIDY_FINDING_RE.findall(output)
    ignored = [f for f in findings if listed_finding(*f)]
    failed = len(ignored) < len(findings) or returncode not in (0, 1) or (returncode == 1 and not findings)
    return failed, len(ignored)


def tidy_command(bdir, unit):
    """clang-tidy on one unit, judging only: fixes come from the serial pass in tidy_fix."""
    return ["clang-tidy", "--quiet", "-p", bdir, unit]


def tidy_cache_dir(bdir):
    """Where lint keeps each unit's clang-tidy result: <BUILD_CACHE_ENV>/tidy in CI (its object cache's steps save it),
    else the build folder's .tidy-cache; None on a tag, which keeps nothing."""
    if tag_ref():
        return None
    root = build_cache_root()
    return root / "tidy" if root else ROOT / bdir / ".tidy-cache"


# In a clang-tidy cache folder: when this run's lint started reading it (`time.time()`), written once per run. Every
# result written or read since is newer (a hit refreshes its time), and every result an earlier run left older, even
# restored by tar with its old times, so prune-build-cache drops what this run didn't use; it deletes the mark too,
# so a saved folder never holds one.
TIDY_RUN_MARK = ".run-start"


def mark_tidy_run(cache):
    mark = Path(cache, TIDY_RUN_MARK)
    if not mark.is_file():
        mark.parent.mkdir(parents=True, exist_ok=True)
        mark.write_text(repr(time.time()), encoding="utf-8")


def compile_entries(bdir, unit):
    """The compile database's entries for `unit`: clang-tidy runs each one."""
    compdb = ROOT / bdir / "compile_commands.json"
    if not compdb.exists():
        return []
    target = os.path.normpath(ROOT / unit)
    return [e for e in json.loads(compdb.read_text(encoding="utf-8"))
            if os.path.normpath(os.path.join(ROOT, e["directory"], e["file"])) == target]


def preprocess_command(entry):
    """(flags, command) for one compile database entry: its command without `-c`, `-o <object>` and the input, then
    that with `-E` and the input, which writes the preprocessed text to stdout."""
    args, flags = list(entry["arguments"]), []
    i = 1
    while i < len(args):
        if args[i] == "-o":
            i += 2
            continue
        if args[i] != "-c" and args[i] != entry["file"]:
            flags.append(args[i])
        i += 1
    flags.insert(0, args[0])
    return flags, [*flags, "-E", entry["file"]]


def preprocessed(entry):
    """The entry's preprocessed text, or None when preprocessing fails (the unit is then linted uncached)."""
    result = subprocess.run(preprocess_command(entry)[1], check=False, cwd=entry["directory"], stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL)
    return result.stdout if result.returncode == 0 else None


def tidy_version():
    return capture(["clang-tidy", "--version"])


def tidy_configs():
    """(path, contents) of every .clang-tidy in the repository, untracked ones too, sorted by path."""
    listed = capture(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", ":(glob)**/.clang-tidy"])
    return [(p, (ROOT / p).read_bytes()) for p in sorted(set(listed.split("\0"))) if p and (ROOT / p).is_file()]


# A line marker in preprocessed text after a newline: `# <line> "<file>"`, the file in C string escapes. Searched from
# the literal newline, not a MULTILINE `^`, which is twice as slow over a unit's ~5 MB.
LINE_MARKER_RE = re.compile(rb'\n# \d+ "((?:[^"\\\n]|\\.)*)"')


def digest(pieces):
    """SHA-256 of `pieces` (bytes or str), each length-prefixed so no two run together into one."""
    h = hashlib.sha256()
    for piece in pieces:
        data = piece if isinstance(piece, bytes) else piece.encode("utf-8")
        h.update(len(data).to_bytes(8, "little") + data)
    return h.hexdigest()


def entry_digest(entry):
    """Digest of one compile database entry's part of tidy_key, or None when it fails to preprocess or its markers don't
    name its own source as a file of ours (ROOT spelt otherwise: a symlink, a bind mount), which would leave every file
    of ours, and so a NOLINT edit, out of the key."""
    text = preprocessed(entry)
    if text is None:
        return None
    root = os.path.normpath(ROOT)
    names = {re.sub(rb"\\(.)", rb"\1", m[1]).decode("utf-8", "replace") for m in LINE_MARKER_RE.finditer(b"\n" + text)}
    paths = {os.path.normpath(os.path.join(entry["directory"], n)) for n in names}
    ours = sorted(p for p in paths if p.startswith(root + os.sep) and os.path.isfile(p))
    if os.path.normpath(os.path.join(root, entry["directory"], entry["file"])) not in ours:
        return None
    return digest([json.dumps(preprocess_command(entry)[0]), text,
                   *itertools.chain.from_iterable((Path(p).relative_to(root).as_posix(), Path(p).read_bytes()) for p in ours)])


def tidy_key(bdir, unit):
    """SHA-256 of all a unit's clang-tidy result depends on: per compile database entry (in any order: XMake exports a
    file two targets compile either way round), its flags (without the input and output paths) and preprocessed text,
    plus the contents of each of our files that text names (comments, NOLINT included, and the macro definitions and
    spacing preprocessing drops); clang-tidy's version and the arguments lint passes it (tidy_command, with placeholders
    for the build folder and the unit, so the folder's location is no input); every .clang-tidy. None when the unit has
    no entry or fails to preprocess: it is then linted uncached."""
    entries = [entry_digest(e) for e in compile_entries(bdir, unit)]
    if not entries or None in entries:
        return None
    command = json.dumps([str(a) for a in tidy_command("<build>", "<unit>")])
    return digest([*sorted(entries), tidy_version(), command, *itertools.chain.from_iterable(tidy_configs())])


def tidy_one(bdir, unit, cache=None):
    """(output, return code) of clang-tidy on one unit, its diagnostics and errors together. With `cache` (a folder), a
    result kept under the unit's tidy_key is returned instead, unless main or a tag must lint cold (cold_ref); a fresh
    result is kept there (never on a tag, nor a crash, which may be the machine's), and a hit refreshes the kept file's
    time (prune-build-cache keeps what a run used)."""
    key = tidy_key(bdir, unit) if cache else None
    stored = Path(cache, f"{key}.json") if key else None
    if stored and not cold_ref():
        try:
            kept = json.loads(stored.read_text(encoding="utf-8"))
            if isinstance(kept["output"], str) and isinstance(kept["returncode"], int):
                os.utime(stored)  # used by this run: prune-build-cache keeps it
                return kept["output"], kept["returncode"]
        except (OSError, ValueError, KeyError, TypeError):
            pass  # absent or corrupt: a miss
    result = subprocess.run(resolve(tidy_command(bdir, unit)), check=False, cwd=ROOT, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, encoding="utf-8", errors="replace")
    if stored and not tag_ref() and result.returncode in (0, 1):
        stored.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=stored.parent, delete=False, suffix=".tmp") as f:
            json.dump({"output": result.stdout, "returncode": result.returncode}, f)
        os.replace(f.name, stored)  # whole or not at all: a run cut short leaves no half-written result
    return result.stdout, result.returncode


def tidy_fix(bdir, units):
    """clang-tidy --fix over `units` in one serial process, as lint ran it before the pool: exported fixes applied with
    clang-apply-replacements would also apply note fix-its that only silence a warning, and one process never writes a
    shared header twice."""
    fix = ["clang-tidy", "--quiet", "-p", bdir, "--fix", *units]
    print("+", " ".join(str(c) for c in fix), flush=True)
    subprocess.run(resolve(fix), check=False, cwd=ROOT)


def tidy_blocks(output):
    """`output` cut into (text, is_finding): the text before the first finding, then each finding with its notes and
    code lines. A shared header's finding repeats in every unit that reaches it; printing each once keeps it readable."""
    starts = [m.start() for m in TIDY_FINDING_RE.finditer(output)]
    bounds = [0, *starts, len(output)]
    return [(output[a:b], a in starts) for a, b in itertools.pairwise(bounds) if a < b]


def tidy_units(bdir, units, fix, cache=None):
    """(failed, ignored findings) of clang-tidy over `units`, one process per unit across all cores, each judged by
    tidy_verdict: run together, one unit's ignored finding would hide another's error. A finding prints once however
    many units report it; a listed one (IGNORED_FINDINGS) never prints, and counts once. `cache` goes to tidy_one. With
    `fix`, the units that failed get one serial clang-tidy --fix afterwards."""
    if cache:
        mark_tidy_run(cache)
    with concurrent.futures.ThreadPoolExecutor(jobs()) as pool:
        results = list(pool.map(lambda unit: tidy_one(bdir, unit, cache), units))
    printed = set()
    for output, _ in results:
        for text, is_finding in tidy_blocks(output):
            finding = TIDY_FINDING_RE.match(text) if is_finding else None
            if finding and listed_finding(finding.group(1), finding.group(2)):
                continue
            key = finding_key(text, finding.group(1) if finding else "")
            if not (is_finding and key in printed):
                printed.add(key)
                print(text, end="", flush=True)
    verdicts = [tidy_verdict(output, returncode) for output, returncode in results]
    failing = [unit for unit, (failed, _) in zip(units, verdicts) if failed]
    if fix and failing:
        tidy_fix(bdir, failing)
    ignored = {finding_key(m.group(0), m.group(1)) for output, _ in results for m in TIDY_FINDING_RE.finditer(output)
               if listed_finding(m.group(1), m.group(2))}
    return any(failed for failed, _ in verdicts), len(ignored)


def check_api_docs(sources):
    """Doxygen (docs/Doxyfile) over the src/ headers among these: an undocumented public class,
    function or member, or a broken doc comment, fails."""
    headers = [f for f in sources if f.startswith("src/") and f.endswith(".hpp")]
    if not headers:
        return
    # Doxygen reports undocumented free functions, variables and enums only in a documented file.
    missing = [h for h in headers if not re.search(r"^\s*///\s*[@\\]file\b", (ROOT / h).read_text(encoding="utf-8"), re.MULTILINE)]
    if missing:
        raise Fail("every src/ header needs a `/// @file` comment saying what it provides:\n" + "\n".join(missing))
    out = ROOT / "build" / "doxygen-lint"
    out.mkdir(parents=True, exist_ok=True)  # Doxygen creates only the last directory of the path
    config = (ROOT / "docs" / "Doxyfile").read_text(encoding="utf-8")
    groups = [p.relative_to(ROOT).as_posix() for p in sorted((ROOT / "src").glob("*/module.dox"))]  # the @ingroup targets
    config += f'\nOUTPUT_DIRECTORY = "{out.as_posix()}"\nINPUT = {" ".join(f'"{h}"' for h in headers + groups)}\n'
    run(["doxygen", "-"], input=config, text=True)


# Shaders: HLSL in shaders/, compiled by `shaders` into committed binaries.
SHADER_BIN = "shaders/bin"
SHADER_MANIFEST = f"{SHADER_BIN}/sources.sha256"  # every source and output with its hash: `lint` finds stale binaries
SHADER_HEADER = "src/gfx/generated/shaders.hpp"  # with the code that includes it; fmt and lint skip generated/
# Per stage (a source is <name>.<stage>.hlsl): its entry point, DXC's profile (shader model 6.0) and the register space SDL_GPU reads
# its resources from; its uniform buffers are in the next space.
SHADER_STAGES = {"vert": ("VertexMain", "vs_6_0", 0), "frag": ("FragmentMain", "ps_6_0", 2)}
SHADER_TOOLS = ("directx-shader-compiler", "spirv-cross", "spirv-tools")  # pixi.toml packages `shaders` runs
MSL_VERSION = "20100"  # MSL 2.1: macOS 10.14, SDL_GPU's minimum for Metal


def shader_resources(reflection, space, name):
    """What a stage binds, counted from SPIRV-Cross's reflection of its SPIR-V, after checking it
    follows SDL_GPU's layout (SDL_CreateGPUShader): in `space`, sampled textures, then storage
    textures, then storage buffers, numbered from 0; uniform buffers from 0 in `space + 1`."""
    if reflection.get("separate_images") or reflection.get("separate_samplers"):
        raise Fail(f"{name}: SDL_GPU's SPIR-V takes combined image samplers: give the texture and its sampler "
                   "[[vk::combinedImageSampler]] and one [[vk::binding]]")
    # Read-only storage only; read-write ones (u registers) need DXIL's separate numbering mapped too.
    writable = [r["name"] for kind in ("images", "ssbos") for r in reflection.get(kind, []) if not r.get("readonly")]
    if writable:
        raise Fail(f"{name}: read-write storage isn't supported by `shaders` yet: {', '.join(writable)}")
    expected = [(kind, space, i) for i, kind in enumerate(k for k in ("textures", "images", "ssbos") for _ in reflection.get(k, []))]
    expected += [("ubos", space + 1, i) for i in range(len(reflection.get("ubos", [])))]
    found = [(kind, r["set"], r["binding"]) for kind in ("textures", "images", "ssbos") for r in sorted(reflection.get(kind, []), key=lambda r: r["binding"])]
    found += [("ubos", r["set"], r["binding"]) for r in sorted(reflection.get("ubos", []), key=lambda r: r["binding"])]
    if found != expected:
        raise Fail(f"{name}: resources at (kind, space, register) {found}, but SDL_GPU expects {expected}: sampled textures, "
                   f"storage textures and storage buffers in space{space} from register 0, uniform buffers in space{space + 1}")
    return {kind: len(reflection.get(kind, [])) for kind in ("textures", "images", "ssbos", "ubos")}


def msl_bindings(reflection, space):
    """DXC options numbering each resource as SDL_GPU's Metal backend does, for SPIRV-Cross to take
    each binding as the Metal index: [[texture]]s and their [[sampler]]s as registered, [[buffer]]s
    uniform buffers first, then storage buffers. DXC wants every resource mapped once one is."""
    def sort(kind):
        return sorted(reflection.get(kind, []), key=lambda r: r["binding"])
    # Each kind in a set of its own (unused by SDL), so no binding clashes in the SPIR-V.
    textures = [(reg, r["binding"], r["binding"], space + 4) for r in sort("textures") for reg in "ts"]
    textures += [("t", r["binding"], r["binding"], space + 4) for r in sort("images")]
    uniforms = [("b", r["binding"], r["binding"], space + 5) for r in sort("ubos")]
    storage = [("t", r["binding"], len(uniforms) + i, space + 6) for i, r in enumerate(sort("ssbos"))]
    return [a for reg, register, binding, metal in textures + uniforms + storage
            for a in ("-fvk-bind-register", f"{reg}{register}", str(space + (reg == "b")), str(binding), str(metal))]


def shader_tool_pins():
    """Each shader tool's pins in pixi.toml, linux-64's and win-64's: one version when they agree."""
    toml = ROOT / "pixi.toml"
    text = toml.read_text(encoding="utf-8") if toml.is_file() else ""
    return {tool: sorted(set(re.findall(rf'^{re.escape(tool)} = "==([^"]+)"', text, re.MULTILINE))) or ["unpinned"]
            for tool in SHADER_TOOLS}


def unequal_shader_pins():
    """The shader tools pinned to different versions for Linux and Windows, which would build different binaries."""
    return [tool for tool, versions in shader_tool_pins().items() if len(versions) > 1]


def shader_hashes():
    """The manifest's content: the shader tools' pins, then each shader source and committed output
    with its SHA-256. A bumped tool makes the binaries stale, as a changed source does."""
    pins = "".join(f"# {tool} ==" + " ==".join(versions) + "\n" for tool, versions in shader_tool_pins().items())
    files = sorted(p for p in [*(ROOT / "shaders").glob("*.hlsl*"), *(ROOT / SHADER_BIN).glob("*"), ROOT / SHADER_HEADER]
                   if p.is_file() and p.relative_to(ROOT).as_posix() != SHADER_MANIFEST)
    return pins + "".join(f"{sha256(p)}  {p.relative_to(ROOT).as_posix()}\n" for p in files)


def stale_shaders():
    """Whether shaders/ and its binaries disagree with the manifest `shaders` wrote with them."""
    manifest = ROOT / SHADER_MANIFEST
    if not (ROOT / "shaders").is_dir():
        return False
    return not manifest.is_file() or manifest.read_text(encoding="utf-8") != shader_hashes()


def lf_endings(path):
    """Rewrite a text file with LF line endings: SPIRV-Cross writes CRLF on Windows, which git would
    store as LF (.gitattributes) and the manifest would then no longer match anywhere."""
    path.write_text(path.read_text(encoding="utf-8"), encoding="utf-8", newline="\n")


def shaders_header(stages):
    """The generated header: each stage's binaries (embedded by XMake's utils.bin2c) and resource counts."""
    lines = ["// Generated by `project_n shaders` from shaders/*.hlsl; don't edit.", "#pragma once", "",
             '#include "core/types.hpp"', "", "#include <array>", "#include <span>", "", "namespace gfx::shaders {", "",
             "/// One shader stage in every format SDL_GPU takes, with what it binds.", "struct Stage {",
             "    /// The stage's name in GPU captures (RenderDoc, PIX, Xcode): `<name>.<stage>`.", "    const char* name;",
             "    /// The HLSL entry point, which keeps its name in every format.", "    const char* entryPoint;",
             "    /// SPIR-V, for Vulkan; no terminating zero, like the other two.", "    std::span<const u8> spirv;",
             "    /// DXIL (shader model 6.0), for D3D12.", "    std::span<const u8> dxil;",
             "    /// MSL source (2.1), for Metal.", "    std::span<const u8> msl;",
             "    /// Combined texture-sampler pairs.", "    u32 samplers;",
             "    /// Read-only storage textures.", "    u32 storageTextures;",
             "    /// Read-only storage buffers.", "    u32 storageBuffers;",
             "    /// Uniform buffers.", "    u32 uniformBuffers;", "};", ""]
    for stem, stage, entry, counts in stages:
        name = f"{stem}_{stage}".upper()
        for fmt, ext in (("SPIRV", "spv"), ("DXIL", "dxil"), ("MSL", "msl")):
            size = (ROOT / SHADER_BIN / f"{stem}.{stage}.{ext}").stat().st_size
            lines += [f"/// shaders/bin/{stem}.{stage}.{ext}, embedded by XMake's utils.bin2c.",
                      f"inline constexpr std::array<u8, {size}> {name}_{fmt}{{{{", f'#include "{stem}.{stage}.{ext}.h"', "}};"]
        lines += [f"/// shaders/{stem}.{stage}.hlsl.", f"inline constexpr Stage {name}{{",
                  f'    .name = "{stem}.{stage}",', f'    .entryPoint = "{entry}",', f"    .spirv = {name}_SPIRV,",
                  f"    .dxil = {name}_DXIL,", f"    .msl = {name}_MSL,", f'    .samplers = {counts["textures"]},',
                  f'    .storageTextures = {counts["images"]},', f'    .storageBuffers = {counts["ssbos"]},',
                  f'    .uniformBuffers = {counts["ubos"]},', "};", ""]
    return "\n".join([*lines, "} // namespace gfx::shaders", ""])


def cmd_shaders(args):
    """Compile shaders/<name>.<stage>.hlsl with DXC to SPIR-V and DXIL, then SPIRV-Cross to MSL, into shaders/bin/
    with the manifest, and the generated header into src/gfx/generated/. The outputs are committed with their sources."""
    if HOST == "macos":
        raise Fail("conda-forge has no DXC or SPIRV-Cross for macOS: run `shaders` on Linux or Windows")
    if unequal_shader_pins():
        raise Fail(f"pin {', '.join(unequal_shader_pins())} to one version for linux-64 and win-64 in pixi.toml")
    sources = sorted((ROOT / "shaders").glob("*.hlsl"))
    for source in sources:  # all checked before any output is removed
        stem, _, stage = source.stem.rpartition(".")
        if stage not in SHADER_STAGES or not re.fullmatch(r"[a-z][a-z0-9_]*", stem):
            raise Fail(f"{source.relative_to(ROOT).as_posix()}: name shaders <name>.<stage>.hlsl, the name in snake_case, "
                       f"the stage one of {', '.join(SHADER_STAGES)}")
    out = ROOT / SHADER_BIN
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    stages = []
    with tempfile.TemporaryDirectory() as tmp:
        for source in sources:
            rel = source.relative_to(ROOT).as_posix()
            stem, _, stage = source.stem.rpartition(".")
            entry, profile, space = SHADER_STAGES[stage]
            base = f"{SHADER_BIN}/{stem}.{stage}"
            dxc = ["dxc", "-T", profile, "-E", entry, rel]
            spirv = [*dxc, "-spirv", "-fspv-target-env=vulkan1.0"]
            run([*spirv, "-Fo", f"{base}.spv"])
            run(["spirv-val", f"{base}.spv"])
            run([*dxc, "-Wno-ignored-attributes", "-Fo", f"{base}.dxil"])  # the [[vk::...]] attributes are SPIR-V's
            reflection = json.loads(capture(["spirv-cross", f"{base}.spv", "--reflect"]))
            counts = shader_resources(reflection, space, f"{rel} ({entry})")
            metal = Path(tmp, f"{stem}.{stage}.spv")
            run([*spirv, *msl_bindings(reflection, space), "-Fo", metal])
            run(["spirv-cross", metal, "--msl", "--msl-version", MSL_VERSION, "--msl-decoration-binding",
                 "--entry", entry, "--stage", stage, "--output", f"{base}.msl"])
            lf_endings(ROOT / f"{base}.msl")
            stages.append((stem, stage, entry, counts))
    (ROOT / SHADER_HEADER).parent.mkdir(parents=True, exist_ok=True)
    (ROOT / SHADER_HEADER).write_text(shaders_header(stages), encoding="utf-8", newline="\n")
    (ROOT / SHADER_MANIFEST).write_text(shader_hashes(), encoding="utf-8", newline="\n")
    print(f"shaders: {len(stages)} stage(s) in {SHADER_BIN}/ and {SHADER_HEADER}; commit them with their sources")


# The atlas step: assigns every sprite in art/sprites.toml a page of the
# texture array and a region on it, and writes them with art/scenes.toml's scenes into the manifest the game reads.
SPRITES_DIR = "assets/sprites"
SPRITE_LIST = "art/sprites.toml"
SCENE_LIST = "art/scenes.toml"
MANIFEST = "assets/manifest.yaml"  # what the game loads: the array's shape, each sprite's place, each scene's sprites
ATLAS_HEADER = "src/core/generated/atlas.hpp"  # the sprite names as SpriteId, which game and gfx compile against; fmt and lint skip generated/
ATLAS_HASHES = "art/atlas.sha256"  # the inputs and outputs with their hashes: `lint` finds a stale or edited manifest or header
ATLAS_PAGE = 2048  # every page of the array is this square
ATLAS_MIPS = 5  # mip levels the game generates for the array
ATLAS_GUTTER = 32  # the least empty pixels between sprites, each owning half, filled with its edges at load: 2 x 16 for two distance fields' reach (core::FIELD_REACH_TEXELS), and at least 2^(ATLAS_MIPS - 1) = 16 so neighbours don't bleed in down to the last mip
ATLAS_ALIGN = 1 << (ATLAS_MIPS - 1)  # every region's x and y are a multiple of the last mip's texel, so each mip level's texels fall on a sprite the same way wherever it lands: a sprite that moves (another resized or reordered before it) keeps its mips


def png_size(path):
    """The width and height in a PNG's header."""
    head = Path(path).read_bytes()[:24]
    if len(head) < 24 or head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        raise Fail(f"{path}: not a PNG")
    return int.from_bytes(head[16:20], "big"), int.from_bytes(head[20:24], "big")


def pack_atlas(sizes, page=ATLAS_PAGE, gutter=ATLAS_GUTTER, align=ATLAS_ALIGN):
    """Shelf packing in the order given: (page, x, y) for each (width, height), every x and y a multiple of `align` and
    at least `gutter` from the page's edge and every other sprite. Each place depends only on the sprites before it, so
    a sprite appended to the list moves no other (art/sprites.toml only appends). Deterministic."""
    def aligned(v):
        return -(-v // align) * align
    start = aligned(gutter)
    if any(start + w + gutter > page or start + h + gutter > page for w, h in sizes):
        raise Fail(f"a sprite is larger than a {page}-pixel page less its {gutter}-pixel gutters")
    pages = []  # per page: [the open shelf's top, its height so far, the next free x]
    places = []
    for w, h in sizes:
        for p, shelf in enumerate(pages):
            if shelf[2] + w + gutter <= page and shelf[0] + h + gutter <= page:
                break  # on the open shelf, which grows to the tallest sprite on it
            below = aligned(shelf[0] + shelf[1] + gutter)
            if below + h + gutter <= page:  # open a new shelf under the full one
                shelf[:] = [below, 0, start]
                break
        else:
            pages.append([start, 0, start])
            p, shelf = len(pages) - 1, pages[-1]
        places.append((p, shelf[2], shelf[0]))
        shelf[1] = max(shelf[1], h)
        shelf[2] = aligned(shelf[2] + w + gutter)
    return places


def atlas_hashes():
    """The hash file's content: the sprite and scene lists, each image, the manifest and the header with their SHA-256s."""
    files = [ROOT / SPRITE_LIST, ROOT / SCENE_LIST, *sorted((ROOT / SPRITES_DIR).glob("*.png")), ROOT / MANIFEST,
             ROOT / ATLAS_HEADER]
    return "".join(f"{sha256(p) if p.is_file() else 'missing'}  {p.relative_to(ROOT).as_posix()}\n" for p in files)


def stale_atlas():
    """Whether the inputs, the manifest or the header disagree with the hashes `atlas` wrote."""
    hashes = ROOT / ATLAS_HASHES
    if not (ROOT / SPRITE_LIST).is_file():
        return False
    return not hashes.is_file() or hashes.read_text(encoding="utf-8") != atlas_hashes()


def read_sprites():
    """The sprite list as (name, width, height, foot x, foot y), in stable-ID order, checked against the images."""
    listed = tomllib.loads((ROOT / SPRITE_LIST).read_text(encoding="utf-8"))
    images = {p.stem for p in (ROOT / SPRITES_DIR).glob("*.png")}
    if set(listed) != images:
        raise Fail(f"{SPRITE_LIST} and {SPRITES_DIR}/ must list the same sprites; only in the list: "
                   f"{sorted(set(listed) - images) or 'none'}, only images: {sorted(images - set(listed)) or 'none'}")
    if not listed:
        raise Fail(f"{SPRITE_LIST} lists no sprites")
    sprites = []
    members = {}
    for name, entry in listed.items():
        if not re.fullmatch(r"[a-z][a-z0-9_]*", name):
            raise Fail(f"{SPRITE_LIST}: name sprites in snake_case: {name}")
        member = sprite_member(name)
        if member in members:
            raise Fail(f"{SPRITE_LIST}: {name} and {members[member]} are both SpriteId::{member}; rename one")
        members[member] = name
        if not isinstance(entry, dict):
            raise Fail(f"{SPRITE_LIST}: {name} must be a table ([{name}])")
        w, h = png_size(ROOT / SPRITES_DIR / f"{name}.png")
        foot = entry.get("foot", [w // 2, h])
        if not (isinstance(foot, list) and len(foot) == 2 and all(type(c) is int for c in foot)):
            raise Fail(f"{SPRITE_LIST}: {name}'s foot must be two integers, [x, y], not {foot!r}")
        fx, fy = foot
        if not (0 <= fx <= w and 0 <= fy <= h):
            raise Fail(f"{SPRITE_LIST}: {name}'s foot {[fx, fy]} is outside its {w}x{h} image")
        sprites.append((name, w, h, fx, fy))
    return sprites


def sprite_member(name):
    """A sprite's SpriteId member: its snake_case name in CamelCase."""
    return "".join(part.title() for part in name.split("_"))


def read_scenes(sprites):
    """The scene list as (name, [sprite names]), in SceneId order, checked against the sprites."""
    listed = tomllib.loads((ROOT / SCENE_LIST).read_text(encoding="utf-8"))
    if not listed:
        raise Fail(f"{SCENE_LIST} lists no scenes")
    known = {name for name, *_ in sprites}
    scenes = []
    for name, entry in listed.items():
        names = entry.get("sprites") if isinstance(entry, dict) else None
        if not (isinstance(names, list) and names and all(isinstance(n, str) for n in names)):
            raise Fail(f"{SCENE_LIST}: {name} must list its sprites, sprites = [\"...\"]")
        if unknown := sorted(set(names) - known):
            raise Fail(f"{SCENE_LIST}: {name} names sprites {SPRITE_LIST} doesn't have: {unknown}")
        if len(set(names)) != len(names):
            raise Fail(f"{SCENE_LIST}: {name} names a sprite twice")
        scenes.append((name, names))
    return scenes


def atlas_manifest(sprites, places, scenes):
    """The manifest the game reads (core::Manifest): the array's shape, each sprite's place and each scene's sprites."""
    pages = max((p for p, _, _ in places), default=-1) + 1
    lines = [f"# Generated by `project_n atlas` from {SPRITE_LIST}, {SCENE_LIST} and {SPRITES_DIR}/; don't edit.",
             f"page_size: {ATLAS_PAGE}", f"pages: {pages}", f"mip_levels: {ATLAS_MIPS}", f"gutter: {ATLAS_GUTTER}",
             f"sprites:  # in {SPRITE_LIST} order: a sprite's index is its SpriteId"]
    lines += [f"  - {{ name: {name}, file: sprites/{name}.png, page: {p}, region: [{x}, {y}, {w}, {h}], foot: [{fx}, {fy}] }}"
              for (name, w, h, fx, fy), (p, x, y) in zip(sprites, places)]
    lines += [f"scenes:  # in {SCENE_LIST} order: a scene's index is its SceneId"]
    lines += [f"  - {{ name: {name}, sprites: [{', '.join(names)}] }}" for name, names in scenes]
    return "\n".join([*lines, ""])


def atlas_header(sprites):
    """The generated header: the sprite names as SpriteId, and as text for checking the manifest against."""
    lines = [f"// Generated by `project_n atlas` from {SPRITE_LIST}; don't edit.",
             "#pragma once", "", '#include "core/types.hpp"', "", "#include <array>", "#include <string_view>", "",
             "namespace core::atlas {", "",
             "/// A sprite's stable ID: its place in art/sprites.toml, and in the manifest's sprites.",
             "enum class SpriteId : u16 { // NOLINT(performance-enum-size): stable IDs outgrow a byte as sprites are added",
             *[f"    {sprite_member(name)}, ///< assets/sprites/{name}.png" for name, *_ in sprites], "};", "",
             "/// Every sprite's name, indexed by SpriteId: the manifest must list the same, in this order.",
             f"inline constexpr std::array<std::string_view, {len(sprites)}> SPRITE_NAMES{{ {{"]
    lines += [f'    "{name}",' for name, *_ in sprites]
    return "\n".join([*lines, "} };", "", "} // namespace core::atlas", ""])


def cmd_atlas(args):
    """Pack the sprites of art/sprites.toml into texture-array pages: the manifest, the header and their hashes."""
    if args.placeholders:
        run([sys.executable, "art/placeholders.py"])
    sprites = read_sprites()
    scenes = read_scenes(sprites)
    places = pack_atlas([(w, h) for _, w, h, _, _ in sprites])
    (ROOT / ATLAS_HEADER).parent.mkdir(parents=True, exist_ok=True)
    (ROOT / MANIFEST).write_text(atlas_manifest(sprites, places, scenes), encoding="utf-8", newline="\n")
    (ROOT / ATLAS_HEADER).write_text(atlas_header(sprites), encoding="utf-8", newline="\n")
    (ROOT / ATLAS_HASHES).write_text(atlas_hashes(), encoding="utf-8", newline="\n")
    print(f"atlas: {len(sprites)} sprite(s) on {max(p for p, _, _ in places) + 1} page(s) and {len(scenes)} scene(s) in "
          f"{MANIFEST} and {ATLAS_HEADER}; commit them with their inputs")


DEPFILES_RE = re.compile(r'depfiles\s*=\s*"((?:[^"\\]|\\.)*)"', re.DOTALL)


def unit_dependencies(bdir):
    """Each unit (a .cpp, repo-relative) to the repo files its XMake record (`.deps/**/*.o.d`, a Lua table holding a
    Make rule) names, itself included. Paths are spelt relative to ROOT or absolute; files outside ROOT are dropped.
    XMake never deletes a record, so only units in the build's fresh compile database count: any other is unrecorded."""
    root = os.path.normpath(ROOT)
    compdb = Path(bdir) / "compile_commands.json"
    compiled = {Path(os.path.relpath(os.path.normpath(os.path.join(root, e["directory"], e["file"])), root)).as_posix()
                for e in (json.loads(compdb.read_text(encoding="utf-8")) if compdb.exists() else [])}
    dependencies = {}
    for record in Path(bdir).glob(".deps/**/*.o.d"):
        m = DEPFILES_RE.search(record.read_text(encoding="utf-8", errors="replace"))
        if not m:
            continue
        rule = re.sub(r"\\(.)", lambda e: {"n": "\n", "t": "\t"}.get(e[1], e[1]), m[1], flags=re.DOTALL)  # Lua's escapes
        names = []
        for token in (t.replace("\\ ", " ") for t in re.split(r"(?<!\\)\s+", rule) if t):  # Make's: `\ ` in a path
            if token.endswith(":"):
                continue  # the rule's target, the object
            path = os.path.normpath(os.path.join(root, token))
            if path.startswith(root + os.sep):
                names.append(Path(os.path.relpath(path, root)).as_posix())
        unit = next((n for n in names if n.endswith(".cpp")), None)
        if unit in compiled:
            dependencies[unit] = dependencies.get(unit, frozenset()) | frozenset(names)
    return dependencies


def lint_units(sources, dependencies):
    """The .cpp files clang-tidy runs on: the changed ones, and those whose records name a changed file (a header's
    findings surface through the units that include it). A unit without a record is always linted."""
    changed = set(sources)
    return [f for f in source_files() if f.endswith(".cpp")
            and (f in changed or f not in dependencies or not dependencies[f].isdisjoint(changed))]


def unreached_headers(sources, dependencies):
    """Changed headers no unit's record names: no unit lints them."""
    reached = set().union(*dependencies.values())
    return sorted(f for f in sources if f.endswith(".hpp") and f not in reached)


# The clone's own git config: the commit hooks, and Git LFS's filters run through Pixi, as any git
# (an editor's, a container's) runs them on large binary assets, not only ours.
CLONE_CONFIG = {
    "core.hooksPath": ".githooks",
    "filter.lfs.process": "pixi run --quiet git-lfs filter-process",
    "filter.lfs.clean": "pixi run --quiet git-lfs clean -- %f",
    "filter.lfs.smudge": "pixi run --quiet git-lfs smudge -- %f",
    "filter.lfs.required": "true",  # a failing filter stops git instead of committing the raw file
}
LFS_POINTER = b"version https://git-lfs.github.com/spec/v1\n"  # how every Git LFS pointer file starts


def clone_set_up():
    return all(capture(["git", "config", "--local", key], check=False).strip() == value for key, value in CLONE_CONFIG.items())


def lfs_listing(listing):
    """(object ID, downloaded, path) for each line of `git lfs ls-files [--long]`: '*' marks a file whose content is
    here, '-' one that is only its pointer. The path is the rest of the line, spaces and dashes included."""
    entries = []
    for line in listing.splitlines():
        oid, marker, path = line.split(" ", 2)
        entries.append((oid, marker == "*", path))
    return entries


def lfs_missing():
    """Git LFS files whose content isn't here, only their pointers, or why that can't be told."""
    if not shutil.which("git-lfs"):
        return ["(git-lfs isn't in the environment)"]
    return [path for _, downloaded, path in lfs_listing(capture(["git", "lfs", "ls-files"])) if not downloaded]


def lfs_pointers(folder):
    """Files under `folder` that are Git LFS pointers rather than their content, relative to it."""
    folder = Path(folder)
    found = []
    for path in sorted(folder.rglob("*")):
        if path.is_file():
            with path.open("rb") as f:
                if f.read(len(LFS_POINTER)) == LFS_POINTER:
                    found.append(path.relative_to(folder).as_posix())
    return found


def lfs_key(listing):
    """CI's cache key for the Git LFS objects of `git lfs ls-files --long`'s `listing`: what this commit needs, whether
    or not they are downloaded yet."""
    objects = "".join(f"{oid} {path}\n" for oid, _, path in lfs_listing(listing))
    return "lfs-" + hashlib.sha256(objects.encode("utf-8")).hexdigest()[:16]


def cmd_lfs_key(args):
    print(lfs_key(capture(["git", "lfs", "ls-files", "--long"])))


# git-lfs keeps objects of "recent" refs and commits too; with every window at zero, prune keeps only the checkout's
# (and unpushed commits', which CI has none of), so CI's cache holds what its key says and no old versions.
LFS_PRUNE = ["git", "-c", "lfs.fetchrecentrefsdays=0", "-c", "lfs.fetchrecentcommitsdays=0",
             "-c", "lfs.fetchrecentremoterefs=false", "-c", "lfs.pruneoffsetdays=0", "lfs", "prune"]


def cmd_lfs_prune(args):
    run(LFS_PRUNE)


# The families of cache entries ci.yml writes, each a key with its changing parts dropped: objects (the env hash, the
# commit), XMake packages (their hash), Git LFS objects (lfs_key). Others (the Pixi envs, the MSVC install and toolset,
# the macOS XMake bundle `xmake-tool-macos-`) are setup caches keyed on what they install, never pruned.
CACHE_FAMILIES = (re.compile(r"(objects-.+)-[0-9a-f]{64}-[0-9a-f]{40}"), re.compile(r"(xmake-(?!tool-).+)-[0-9a-f]{64}"),
                  re.compile(r"(lfs)-[0-9a-f]{16}"))


def cache_family(key):
    """The family of a cache key (CACHE_FAMILIES), or None for one never pruned."""
    return next((m[1] for m in (f.fullmatch(key) for f in CACHE_FAMILIES) if m), None)


def refused(error):
    """Whether gh failed because the token may not do it: Dependabot's runs get a read-only one."""
    return "HTTP 403" in (error.stderr or "")


def cmd_prune_ci_caches(args):
    """CI's cleanup job: of this ref's cache entries, keep the newest of each family and version (one key saved from two
    operating systems is two entries) and delete the rest, which no restore of this ref reads again: each restore takes
    the newest match. Other refs' entries are never touched."""
    ref = os.environ.get("GITHUB_REF")
    if not ref:
        raise Fail("prune-ci-caches prunes the Actions caches of the ref CI is building (GITHUB_REF): CI runs it")
    deleted = []
    try:
        entries = json.loads(capture(["gh", "cache", "list", "--ref", ref, "--limit", "1000",
                                      "--json", "id,key,version,sizeInBytes,createdAt"]))
        newest, superseded = {}, []
        for entry in sorted(entries, key=lambda e: e["createdAt"], reverse=True):
            family = cache_family(entry["key"])
            if not family:
                continue
            if (family, entry["version"]) in newest:
                superseded.append(entry)
            else:
                newest[(family, entry["version"])] = entry
        for entry in superseded:
            try:
                capture(["gh", "cache", "delete", str(entry["id"])])
                deleted.append(entry)
            except subprocess.CalledProcessError as e:
                if "HTTP 404" not in (e.stderr or ""):
                    raise  # a 404: another run of this ref deleted it first
    except subprocess.CalledProcessError as e:
        if not refused(e):
            raise
        done = f"deleted {len(deleted)} before that" if deleted else "nothing deleted"
        print(f"prune-ci-caches: the token may not manage {ref}'s caches (HTTP 403, a read-only token); {done}")
        return 0
    print(f"prune-ci-caches: deleted {len(deleted)} superseded cache entries of {ref} "
          f"({mib(e['sizeInBytes'] for e in deleted)}), kept {len(newest)}")
    return 0


def check_lfs_pointers(rng):
    """Fails on a commit in `rng` (none: HEAD) that stores a file .gitattributes puts in Git LFS as a plain blob."""
    try:
        run(["git", "lfs", "fsck", "--pointers", *([rng] if rng else [])])
    except subprocess.CalledProcessError:
        raise Fail("a file .gitattributes stores in Git LFS was committed as a plain blob, or a pointer is malformed (above): "
                   "run `pixi run project_n setup`, then `git add --renormalize <file>` and amend") from None


def cmd_setup(args):
    for key, value in CLONE_CONFIG.items():
        run(["git", "config", "--local", key, value])
    run(["git", "lfs", "pull"])  # replaces the pointers a clone without the filters checked out


def check_staged_format():
    """The pre-commit hook: the staged content of each staged source, as it will be committed. A staged
    .clang-format (at any depth) or pixi.lock (the clang-format version) checks every source."""
    staged = capture(["git", "diff", "--cached", "--name-only", "--no-renames"]).splitlines()
    indexed = capture(["git", "ls-files"]).splitlines()  # the index, not the disk: a staged file may be gone
    every = any(Path(f).name == ".clang-format" or f == "pixi.lock" for f in staged)
    files = sorted(f for f in (indexed if every else set(staged) & set(indexed)) if is_source(f))
    bad = []
    for f in files:
        r = subprocess.run(resolve(["clang-format", "--dry-run", "-Werror", f"--assume-filename={f}"]), cwd=ROOT, check=False,
                           input=capture(["git", "show", f":{f}"]), capture_output=True, encoding="utf-8")
        if r.returncode:
            print(r.stderr, end="")
            bad.append(f)
    if bad:
        raise Fail("not formatted: " + ", ".join(bad) + "\nrun `./dev fmt` (Windows: `dev fmt`), stage the result and commit again")


def cmd_fmt(args):
    if args.staged:
        return check_staged_format()
    files, _ = scoped_sources(args)
    if not files:
        print("fmt: no changed sources (--all covers every file)")
        return
    run(["clang-format", *(["--dry-run", "-Werror"] if args.check else ["-i"]), *files])


def cmd_lint(args):
    if needs_container(LINT_PRESET, args.container):
        rng = args.range or scope_range()  # resolved here: the container may see another upstream
        return reenter(LINT_PRESET, ["lint", *(["--fix"] if args.fix else []), *(["--all"] if args.all else []),
                                     *(["--range", rng] if rng else [])])
    sources, changed = scoped_sources(args)
    referencing = design_reference_findings(None if changed is None else sorted(changed))
    if referencing:
        raise Fail("\n".join(referencing))
    found = unexplained_suppressions(sources)
    if found:
        raise Fail("fix findings properly; a suppression is the last resort and must be narrow and explained:\n" + "\n".join(found))
    leaked = leaked_dependencies(sources)
    if leaked:
        raise Fail("third-party and platform headers belong in .cpp files, behind our own types (src/AGENTS.md):\n"
                   + "\n".join(leaked))
    crossed = crossed_modules(sources)
    if crossed:
        raise Fail("a module includes only its own headers and those of the modules below it, and its internal/ headers "
                   "stay inside it (MODULES in tools/project_n.py):\n" + "\n".join(crossed))
    standard = standard_includes_in_sources(sources)
    if standard:
        raise Fail("standard headers come from the precompiled header, not from .cpp files (src/AGENTS.md):\n"
                   + "\n".join(standard))
    unsized = unsized_types(sources)
    if unsized:
        raise Fail("use core/types.hpp's sized types (i32, u8, f32...) (src/AGENTS.md):\n" + "\n".join(unsized))
    bypassed = bypassed_facades(sources)
    if bypassed:
        raise Fail("use the facades: log through PN_LOG_* (core/log.hpp), profile through PN_PROFILE_* (core/profile.hpp), "
                   "and reach EnTT through core/ecs.hpp (FACADES, src/AGENTS.md):\n"
                   + "\n".join(bypassed))
    maths = platform_maths(sources)
    if maths:
        raise Fail("the simulation uses core::math (core/math.hpp), not the platform's maths functions:\n"
                   + "\n".join(maths))
    check_api_docs(sources)
    if unequal_shader_pins():
        raise Fail(f"pin {', '.join(unequal_shader_pins())} to one version for linux-64 and win-64 in pixi.toml: "
                   "both build the committed shader binaries")
    if stale_shaders():
        raise Fail(f"shaders/ changed but its binaries didn't, or they were edited ({SHADER_MANIFEST}): run `./dev shaders` "
                   "on Linux or Windows and commit the result")
    if stale_atlas():
        raise Fail(f"art/sprites.toml, art/scenes.toml or assets/sprites/ changed but the atlas didn't, or it was edited ({ATLAS_HASHES}): "
                   "run `./dev atlas` and commit the result")
    check_lfs_pointers(args.range or scope_range())
    if changed is None or any(f.startswith(("tools/", "art/")) and f.endswith(".py") for f in changed):
        run(["ruff", "check", "tools", "art"])
    if changed is None or any(f.startswith(".github/") for f in changed):
        run(["actionlint"])
    if not any(f.endswith((".cpp", ".hpp")) for f in sources):
        print("lint: no changed C++ sources (--all covers every file)")
        return
    bdir = configure(LINT_PRESET, "debug")
    run(["xmake", "build"], env=xmake_env(bdir))
    run([*COMPDB, bdir], env=xmake_env(bdir))
    # The build just refreshed every unit's record of what it includes: lint the units a change reaches.
    dependencies = unit_dependencies(bdir)
    units = lint_units(sources, dependencies)
    for header in unreached_headers(sources, dependencies):
        print(f"lint: {header} reaches no unit, not linted")
    # Lint is a gate: any finding in our own files fails `check`.
    try:
        print(f"+ clang-tidy --quiet -p {bdir} on {len(units)} unit(s), {jobs()} at a time"
              + (", then --fix on the units that fail" if args.fix else ""), flush=True)
        failed, ignored = tidy_units(bdir, units, args.fix, tidy_cache_dir(bdir))
        if failed:
            raise Fail("clang-tidy failed: findings in our code, a compiler error or a crash (above)")
        if ignored:
            print(f"lint: ignored {ignored} listed finding(s) in library code (IGNORED_FINDINGS)")
    finally:
        if args.fix:  # clang-tidy's fixes don't follow .clang-format
            cmd_fmt(argparse.Namespace(check=False, staged=False, all=args.all, range=args.range))


def commit_range():
    """Commits not yet on the upstream (or on main): what `check` validates. CI passes its own."""
    if os.environ.get("PROJECT_N_COMMIT_RANGE"):
        return os.environ["PROJECT_N_COMMIT_RANGE"]
    upstream = subprocess.run(["git", "rev-parse", "--abbrev-ref", "@{upstream}"], check=False, cwd=ROOT, capture_output=True, text=True)
    if upstream.returncode == 0:
        return f"{upstream.stdout.strip()}..HEAD"
    if capture(["git", "rev-parse", "--abbrev-ref", "HEAD"]).strip() != "main":
        return "main..HEAD"
    return None


def step_times_table(times):
    """`check`'s steps and how long each took, then the total: what to look at when the loop feels slow."""
    width = max((len(name) for name, _ in times), default=0)
    lines = [f"  {name:<{width}}  {seconds:7.1f} s" for name, seconds in times]
    return "\n".join(["check: step times", *lines, f"  {'total':<{width}}  {sum(s for _, s in times):7.1f} s"])


def cmd_check(args):
    """The merge gate: commit messages, formatting and lint of the diff, then build and test every
    preset. The pre-commit hook checks formatting at each commit, so the hooks must be on; this
    format check covers what it misses (rebases and cherry-picks don't run it)."""
    if not clone_set_up():
        raise Fail("set the clone up first (the commit hooks, Git LFS's filters): pixi run project_n setup")
    sweep_here(check_trees(args))
    times = []

    def step(name, command, *step_args):
        start = time.monotonic()
        try:
            return command(*step_args)
        finally:
            times.append((name, time.monotonic() - start))

    try:
        skipped = check_steps(args, step)
        print("check: all presets passed" + (" with every supported sanitizer" if args.sanitizers else "")
              + (f" (skipped {', '.join(skipped)}: not buildable on this host)" if skipped else ""))
    finally:
        print(step_times_table(times), flush=True)


def check_trees(args):
    """The trees cmd_check's steps build in, found by walking the steps without running them, so the two can't
    drift apart."""
    trees = []

    def collect(name, command, *step_args):
        if command is cmd_lint:
            trees.extend(own_trees([LINT_PRESET], "debug", container=args.container))
        elif command is cmd_reference:
            trees.extend(own_trees([step_args[0].preset], "release", container=args.container))
        elif command is cmd_test or command is cmd_build:
            a = step_args[0]
            trees.extend(own_trees([a.preset], a.mode, a.sanitizer, getattr(a, "unity", False), a.container))

    check_steps(args, collect)
    return trees


def check_steps(args, step):
    """cmd_check's steps, each run through `step(name, command, args)`, which times it; the presets skipped."""
    step("selftest", cmd_selftest, args)
    rng = commit_range()
    if rng and step("commit-check", cmd_commit_check, argparse.Namespace(range=rng, message_file=None)):
        raise Fail("commit-check failed")
    step("fmt", cmd_fmt, argparse.Namespace(check=True, staged=False, all=False, range=None))
    step("lint", cmd_lint, argparse.Namespace(container=args.container, fix=False, all=False, range=None))
    skipped = [p for p in PRESETS if not buildable_here(p) or (args.container and not PRESETS[p]["image"])]
    for preset in [p for p in PRESETS if p not in skipped]:
        step(f"test {preset} release", cmd_test, argparse.Namespace(
            preset=preset, mode="release", sanitizer="none", container=args.container, target=None, test=None, args=[],
            unity=False))
    linux = next((p for p in PRESETS if PRESETS[p]["plat"] == "linux" and p not in skipped), None)
    if linux:
        step(f"reference {linux}", cmd_reference, argparse.Namespace(preset=linux, container=args.container, update=False))
    # Unity builds are off (xmake.lua) but kept working: one test run with them on.
    unity_preset = NATIVE_PRESET if NATIVE_PRESET not in skipped else next(p for p in PRESETS if p not in skipped)
    step(f"unity test {unity_preset} debug", cmd_test, argparse.Namespace(
        preset=unity_preset, mode="debug", sanitizer="none", container=args.container, target=None, test=None, args=[],
        unity=True))
    # Profiling builds turn Tracy's zones on, code no other mode compiles: one build keeps it compiling.
    step(f"build {unity_preset} releasedbg", cmd_build, argparse.Namespace(
        preset=unity_preset, mode="releasedbg", sanitizer="none", container=args.container))
    if args.sanitizers:
        for preset, sanitizers in SANITIZERS.items():
            for sanitizer in sanitizers if preset not in skipped else ():
                step(f"test {preset} debug {sanitizer}", cmd_test, argparse.Namespace(
                    preset=preset, mode="debug", sanitizer=sanitizer, container=args.container, target=None, test=None,
                    args=[], unity=False))
    return skipped


def msvc_versions(env_sh):
    """The MSVC and SDK versions msvc-wine's wrappers use: the patch levels follow Microsoft's manifest."""
    found = dict(re.findall(r"^(MSVCVER|SDKVER)=(\S+)$", env_sh, re.MULTILINE))
    return {"msvc": found.get("MSVCVER"), "windows_sdk": found.get("SDKVER")}


def tree_dirty():
    """Uncommitted changes, except xmake-requires.lock: builds rewrite its order without changing it
    (tools/AGENTS.md). A real lock change comes with xmake.lua or xmake/repo changes, which count."""
    return capture(["git", "status", "--porcelain", "--", ".", ":!xmake-requires.lock"]).strip()


def info_plist(version):
    # The oldest macOS comes from Pixi's MACOSX_DEPLOYMENT_TARGET (pixi.toml), like the compiler's.
    return {"CFBundleIdentifier": BUNDLE_ID, "CFBundleName": APP, "CFBundleExecutable": APP, "CFBundlePackageType": "APPL",
            "CFBundleShortVersionString": version, "CFBundleVersion": version,
            "LSMinimumSystemVersion": os.environ["MACOSX_DEPLOYMENT_TARGET"], "LSApplicationCategoryType": "public.app-category.games"}


def macos_bundle(dist, binary):
    """dist/<APP>.app: the binary without its rpath into the Pixi environment, conda's libc++ in Frameworks (the
    rpath in xmake.lua), the assets in Resources (where SDL_GetBasePath points in a bundle) and Info.plist."""
    contents = dist / f"{APP}.app" / "Contents"
    for d in ("MacOS", "Frameworks", "Resources"):
        (contents / d).mkdir(parents=True)
    shutil.copy2(binary, contents / "MacOS")
    # From the copy only: `artifact --reuse-build` doesn't relink the build folder's binary, whose rpath would be gone
    # the next time ("no LC_RPATH load command").
    run(["install_name_tool", "-delete_rpath", Path(os.environ["CONDA_PREFIX"], "lib"), contents / "MacOS" / binary.name])
    shutil.copytree(ROOT / "assets", contents / "Resources" / "assets")
    shutil.copy2(Path(os.environ["CONDA_PREFIX"], "lib", "libc++.1.dylib"), contents / "Frameworks")
    (contents / "Info.plist").write_bytes(plistlib.dumps(info_plist(read_version())))
    # Ad-hoc signature (O6), nested code first: Apple's order, as --deep is deprecated. arm64 refuses
    # to run code whose signature install_name_tool broke.
    run(["codesign", "--force", "--sign", "-", contents / "Frameworks" / "libc++.1.dylib"])
    run(["codesign", "--force", "--sign", "-", contents.parent])
    run(["codesign", "--verify", "--strict", contents.parent])


def ci_artifact_name(preset):
    """The name CI uploads a preset's archives under: the version and preset, as in the archives'
    names, plus the commit unless CI is building a tag (a dev build isn't that version yet)."""
    name = f"{APP}-{read_version()}-{preset}"
    if not tag_ref():
        name += "-" + capture(["git", "rev-parse", "--short=7", "HEAD"]).strip()
    return name


def write_ci_artifact_name(preset):
    """Hands CI's upload step its artifact name (steps.artifact.outputs.archive in ci.yml)."""
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as out:
        out.write(f"archive={ci_artifact_name(preset)}\n")


def cmd_artifact(args):
    # Release artefacts come from committed sources only; the container sees the whole checkout.
    if tree_dirty():
        raise Fail("artifact builds committed sources only: commit or stash first (`check --container` tests uncommitted work)")
    pointers = lfs_pointers(ROOT / "assets")
    if pointers:
        raise Fail("assets/ holds Git LFS pointers, not the files: run `pixi run project_n setup`\n" + "\n".join(pointers))
    if "GITHUB_OUTPUT" in os.environ and not IN_CONTAINER:
        write_ci_artifact_name(args.preset)
    reuse = args.reuse_build
    if not IN_CONTAINER and (PRESETS[args.preset]["image"] or needs_container(args.preset)):
        return reenter(args.preset, ["artifact", "--preset", args.preset, *(["--reuse-build"] if reuse else [])])
    if reuse and cold_ref():
        print(f"artifact: CI builds {os.environ['GITHUB_REF']} from source, so the clean room rebuilds (--reuse-build refused)")
    if not reuse or cold_ref():
        shutil.rmtree(ROOT / build_dir(args.preset, "release"), ignore_errors=True)  # clean room: nothing from earlier builds
    bdir = build(args.preset, "release")
    run(["xmake", "test", "--output-on-failure"], env=xmake_env(bdir))
    if PRESETS[args.preset]["plat"] == "linux":
        run_references(args.preset)
    binary = binary_path(args.preset, "release")
    if PRESETS[args.preset]["plat"] == "linux":
        # conda's GCC hard-codes an rpath into .pixi. Edited in place, unlike macOS (macos_bundle): patchelf succeeds on
        # a binary with no rpath, so a reused build packages again.
        run(["patchelf", "--remove-rpath", binary])
        leaked = [line for line in capture(["ldd", binary]).splitlines() if ".pixi" in line]
        if leaked:
            raise Fail("artefact links into the Pixi environment:\n" + "\n".join(leaked))
    elif PRESETS[args.preset]["plat"] == "macosx":
        shipped = ("/usr/lib/", "/System/", "@rpath/libc++.1.dylib")  # libc++ ships in the bundle
        leaked = [line for line in capture(["otool", "-L", binary]).splitlines()[1:] if not line.strip().startswith(shipped)]
        if leaked:
            raise Fail("artefact links libraries macOS doesn't ship:\n" + "\n".join(leaked))
    dist, syms = ROOT / "dist" / args.preset, ROOT / "dist" / f"{args.preset}-symbols"
    for d in (dist, syms):
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)
    if PRESETS[args.preset]["plat"] == "macosx":
        macos_bundle(dist, ROOT / binary)
    else:
        shutil.copy2(ROOT / binary, dist)
        shutil.copytree(ROOT / "assets", dist / "assets")  # beside the binary: platform::AssetsDirectory
    for notice in ("LICENSE", "THIRD_PARTY_NOTICES.md"):  # AGPL and the bundled libraries' licences
        shutil.copy2(ROOT / notice, dist)
    # Debug info XMake split off the release binary: kept with the release for crash reports, never shipped.
    symbols = ROOT / binary.with_suffix({"linux": ".sym", "windows": ".pdb", "macosx": ".dSYM"}[PRESETS[args.preset]["plat"]])
    if not symbols.exists():
        raise Fail(f"no debug symbols at {symbols}: XMake skips the split when its tool (objcopy, dsymutil) is missing")
    (shutil.copytree if symbols.is_dir() else shutil.copy2)(symbols, syms / symbols.name)
    manifest = {
        "version": read_version(),
        "commit": capture(["git", "rev-parse", "HEAD"]).strip(),
        "preset": args.preset,
        "image": os.environ.get("PROJECT_N_IMAGE", "unknown"),
        "pixi_lock_sha256": sha256("pixi.lock"),
        "files": {p.relative_to(dist.parent).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in sorted([*dist.rglob("*"), *syms.rglob("*")]) if p.is_file()},
    }
    # Toolchain parts pixi.lock doesn't pin.
    if PRESETS[args.preset]["toolchain"] == "msvc":
        manifest |= msvc_versions(Path("/opt/msvc/bin/x64/msvcenv.sh").read_text(encoding="utf-8"))
    elif PRESETS[args.preset]["plat"] == "macosx":
        manifest["macos_sdk"] = capture(["xcrun", "--show-sdk-version"]).strip()
    (dist / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    # Upload-ready archives for a hosted release: the game, and its symbols separately.
    stem = f"{ROOT / 'dist' / APP}-{manifest['version']}-{args.preset}"
    shutil.make_archive(stem, "zip", dist)
    shutil.make_archive(f"{stem}-symbols", "zip", syms)
    print(f"artifact: {dist.relative_to(ROOT)}, {Path(stem).name}.zip and -symbols.zip")


def cmd_coverage(args):
    """Line coverage of src/ by the tests: GCC's gcov data, reported by gcovr into build/coverage/."""
    preset = "linux-gcc"
    if needs_container(preset, args.container):
        return reenter(preset, ["coverage"])
    for stale in (ROOT / build_dir(preset, "coverage")).rglob("*.gcda"):  # counts add up across runs
        stale.unlink()
    cmd_test(argparse.Namespace(preset=preset, mode="coverage", sanitizer="none", container=False, target=None, test=None, args=[],
                                unity=False))
    out = ROOT / "build" / "coverage"
    out.mkdir(parents=True, exist_ok=True)
    run(["gcovr", "--root", ROOT, "--filter", "src/", "--gcov-executable", "x86_64-conda-linux-gnu-gcov",
         "--html-details", out / "index.html", "--print-summary", build_dir(preset, "coverage")])


def cmd_fuzz(args):
    """Fuzz one libFuzzer target (tests/fuzz/<name>.cpp): linux-clang with AddressSanitizer."""
    preset = "linux-clang"
    if needs_container(preset, args.container):
        return reenter(preset, ["fuzz", args.target, "--time", str(args.time)])
    bdir = build(preset, "debug", "address")
    work = ROOT / "build" / "fuzz" / args.target  # new inputs and crash files; seeds stay in tests/fuzz/corpus/
    (work / "corpus").mkdir(parents=True, exist_ok=True)
    run(["xmake", "run", f"fuzz_{args.target}", work / "corpus", ROOT / "tests" / "fuzz" / "corpus" / args.target,
         f"-max_total_time={args.time}", f"-artifact_prefix={work}/"], env=xmake_env(bdir))


def cmd_docs(args):
    (ROOT / "build" / "docs").mkdir(parents=True, exist_ok=True)  # Doxygen creates only the last directory
    run(["doxygen", "docs/Doxyfile"])
    print(f"docs: {ROOT / 'build' / 'docs' / 'html' / 'index.html'}")


def cmd_profiler(args):
    # The viewer lives in its own Pixi environment (pixi.toml: feature.profiler).
    run([os.environ.get("PIXI_EXE", "pixi"), "run", "-e", "profiler", "tracy-profiler", *([Path(args.file).resolve()] if args.file else [])])


def cmd_profile(args):
    if PRESETS[args.preset]["host"] != HOST:
        raise Fail(f"`profile` is native only; {args.preset} needs a {PRESETS[args.preset]['host']} host")
    if args.seconds < 1:
        raise Fail("--seconds takes a whole number above 0")
    bdir = build(args.preset, "releasedbg")
    recorder = profiler_tool("tracy-capture", "tracy-profiler-capture")
    out = ROOT / "build" / "profiles" / f"{time.strftime('%Y%m%d-%H%M%S')}.tracy"
    extra = args.args[1:] if args.args[:1] == ["--"] else args.args
    record(recorder, ["xmake", "run", APP, "--quit-after", str(args.seconds), *extra], out, env=xmake_env(bdir))
    print(f"profile: {out} (open it with `project_n profiler {out.relative_to(ROOT).as_posix()}`)")


def profiler_tool(program, package):
    """The path of `program` from Pixi's profiler environment (pixi.toml: feature.profiler), which `package` installs."""
    prefix = pixi_prefix("profiler")
    # Windows' conda packages put programs in Library/bin.
    tool = shutil.which(program, path=os.pathsep.join(str(prefix / d) for d in ("bin", "Library/bin")))
    if not tool:
        raise Fail(f"no {program} in {prefix}: is {package} still in pixi.toml's profiler feature?")
    return tool


def record(recorder, game, out, **kwargs):
    """Run the command line `game`, which quits by itself, while Tracy's `recorder` (tracy-capture) records it into
    `out`; `kwargs` go to `run`. A failed or unfinished recording leaves no file."""
    out.parent.mkdir(parents=True, exist_ok=True)
    # Nothing is clicked, so the game keeps the focus and never pauses itself. It quits by
    # itself; the recorder stops when the game disconnects, after Tracy has sent the rest.
    print("+", recorder, "-o", out, flush=True)
    recording = subprocess.Popen([recorder, "-o", str(out)], cwd=ROOT)
    try:
        run(game, **kwargs)
        recording.wait(timeout=60)
        if recording.returncode != 0 or not out.exists():
            raise Fail(f"tracy-capture failed with status {recording.returncode}; nothing recorded")
    except BaseException as e:
        if recording.poll() is None:
            recording.terminate()
            recording.wait()
        out.unlink(missing_ok=True)  # a half-written capture isn't offered to `profiler`
        if isinstance(e, subprocess.TimeoutExpired):
            raise Fail("tracy-capture didn't finish within 60 s of the game quitting; nothing recorded") from None
        raise


# `measure`'s runs: name -> (game arguments, seconds before the game quits). The arena runs last as long as they did when first
# profiled; the world path quits the game when it ends (about 26 s), its limit only a safety net.
MEASURE_RUNS = {"world": (["--world", "--camera-path"], 60), "burst": (["--burst"], 20),
                "swords": (["--cast-swords"], 70), "seals": (["--cast-swords", "--zoom", "2"], 30)}
MEASURE_WORKERS = 2
# Fixed conditions: the display's native size, and a present the display's refresh doesn't pace.
MEASURE_FLAGS = ["--fullscreen", "--workers", str(MEASURE_WORKERS), "--unpaced"]
# The arena runs' numbers from before the job system could be pinned, printed beside the new ones.
MEASURE_EARLIER = {"burst": "0.43 ms median, 2.6 ms worst (main thread)",
                   "swords": "1,000 swords 1.31/2.31/2.60 ms (median/p99/worst frame, at a volley's peak)",
                   "seals": "~685 µs median frame"}
HITCH_MS = 16.7  # a frame whose work is longer misses a 60 Hz display's refresh
# Each camera segment's start is measured apart: the camera cuts or turns onto new ground there.
NEW_GROUND_SECONDS = 0.5
SETTLE_SECONDS = 2.0  # an arena run's start, left out: the scene loads (the camera path waits as long)
MS = 1_000_000  # nanoseconds
FRAME_ZONE = ("Run", "src/app/app.cpp")  # App::Run's loop body: one zone a frame, on the main thread
GPU_WAIT_ZONE = "WaitForGpu"  # the wait for the next swapchain image: the GPU's share while presenting unpaced
IDLE_ZONES = (GPU_WAIT_ZONE, "WaitUntilNs")  # zones that only wait, never a thread's work
LOGIC_FILE = "src/core/logic.cpp"  # names the logic thread
JOBS_FILE = "src/core/jobs.cpp"  # names the job system's workers (the main and logic threads run its zones too)
P99_FRAMES = 100  # below this many frames the 99th percentile by nearest rank is the worst: printed as —
# A segment whose median frame is within this share of the display's refresh interval is paced by the display: its
# GPU share is then the wait to present, not the GPU's work.
PACED_TOLERANCE = 0.03
# tracy-csvexport's column separator (`-s`): the ASCII unit separator, which no column holds. Its exports aren't
# quoted, and a zone's name (MSVC names keep template arguments: `Foo<A,B>::Bar`), its text and a message may all hold
# commas.
EXPORT_SEP = "\x1f"
SEGMENT_PREFIX = "camera path: "  # the Tracy message each camera path segment begins with (src/app/app.cpp)
LATENCY_RE = re.compile(r"input latency: ([\d.]+) ms")
SHIMMER_RE = re.compile(r"world shimmer: (\S+)")
DISPLAY_RE = re.compile(r"display: (\d+x\d+ at \S+ Hz(?: \([^)\n]*\))?)")
REFRESH_RE = re.compile(r"at ([\d.]+) Hz")
PAUSED_RE = re.compile(r"paused: the window is unfocused or minimised")
PATH_WARNING_RE = re.compile(r"^.*WARNING\s+\S+\s+(.*camera path.*)$", re.MULTILINE)


@dataclasses.dataclass
class Timeline:
    """A run's frames from its main thread's frame zones: frame i runs from starts[i] to starts[i + 1] (ns since the
    capture began), waits[i] of it for the GPU."""
    starts: list
    waits: list
    main: str  # the main thread's number in the export

    def frame(self, i):
        return self.starts[i + 1] - self.starts[i]

    def work(self, i):
        return self.frame(i) - self.waits[i]

    def index(self, ns):
        """The frame running at `ns`, if any."""
        i = bisect.bisect_right(self.starts, ns) - 1
        return i if 0 <= i < len(self.starts) - 1 else None


@dataclasses.dataclass
class Work:
    """What each thread did in a run's frames."""
    busy: dict  # thread name -> each frame's work on it, ns: the self time of its zones starting in the frame
    zones: dict  # hitch frame -> the main thread's zones' self times in it by name


@dataclasses.dataclass
class Segment:
    """One part of a run's numbers: a camera segment's start or rest, or an arena run's whole."""
    label: str
    frames: int
    rows: list  # (what, (median, p99 or None, worst) ms): the whole frame, its work and each thread's
    gpu: float  # the share of the frame time spent waiting for the GPU
    hitches: list  # (start ns since the run's first frame, work ns, frame ns, its three largest zones as (name, ns))


@dataclasses.dataclass
class MeasuredRun:
    """A run's numbers and what its game log says about them."""
    name: str
    display: str
    latencies: list  # input latencies, ms
    reasons: list  # why its numbers can't be trusted; none when they can
    segments: list


def measure_runs(text):
    """The run names `--runs` lists (all of them without it), in order and once each; unknown names are refused."""
    names = list(dict.fromkeys(name.strip() for name in (text.split(",") if text is not None else MEASURE_RUNS)))
    unknown = [name for name in names if name not in MEASURE_RUNS]
    if unknown or not names:
        raise Fail(f"measure: unknown run(s) {', '.join(unknown) or 'none named'}; the runs are {', '.join(MEASURE_RUNS)}")
    return names


def percentiles(values):
    """(median, 99th percentile, worst) of `values`; the percentile by nearest rank, None below P99_FRAMES values,
    where it would be the worst."""
    ordered = sorted(values)
    p99 = ordered[-(-len(ordered) * 99 // 100) - 1] if len(ordered) >= P99_FRAMES else None
    return statistics.median(ordered), p99, ordered[-1]


def zone_rows(lines):
    """(name, file, start ns, self ns, thread) of each zone in `tracy-csvexport -u -e` output."""
    for line in lines:
        if line.startswith(f"name{EXPORT_SEP}src_file{EXPORT_SEP}"):
            continue
        name, file, _, start, took, thread, _ = line.rstrip("\r\n").split(EXPORT_SEP, 6)
        yield name, file, int(start), int(took), thread


def frame_timeline(lines):
    """The frames of a run's export (the main thread's frame zones) with each one's wait for the GPU."""
    starts, waits, main = [], [], None
    wanted = (f"{FRAME_ZONE[0]}{EXPORT_SEP}", f"{GPU_WAIT_ZONE}{EXPORT_SEP}")
    for name, file, start, took, thread in zone_rows(line for line in lines if line.startswith(wanted)):
        if name == FRAME_ZONE[0] and file.endswith(FRAME_ZONE[1]):
            starts.append(start)
            main = thread
        elif name == GPU_WAIT_ZONE:
            waits.append((start, took, thread))
    if len(starts) < 2:
        raise Fail(f"measure: the capture holds {len(starts)} frame(s): did the game run?")
    timeline = Timeline(sorted(starts), [0] * (len(starts) - 1), main)
    for start, took, thread in waits:
        if thread == main and (i := timeline.index(start)) is not None:
            timeline.waits[i] += took
    return timeline


def hitch_frames(timeline, frames):
    """The frames among `frames` whose work, their time less the wait for the GPU, is over HITCH_MS."""
    return [i for i in frames if timeline.work(i) > HITCH_MS * MS]


def frame_work(lines, timeline, hitches):
    """Each thread's work in each frame of a run's export, and the main thread's zones in each of the `hitches`
    frames. A zone counts in the frame it starts in. Threads are named by what they ran: main, logic, a worker (job
    system zones), or else a thread; a name two threads share takes each one's number."""
    busy = collections.defaultdict(lambda: [0] * (len(timeline.starts) - 1))
    zones = {i: collections.Counter() for i in hitches}
    logic, workers = set(), set()
    for name, file, start, took, thread in zone_rows(lines):
        if file.endswith(LOGIC_FILE):
            logic.add(thread)
        elif file.endswith(JOBS_FILE):
            workers.add(thread)
        i = timeline.index(start)
        if i is None or name in IDLE_ZONES:
            continue
        busy[thread][i] += took
        if thread == timeline.main and i in zones:
            zones[i][name] += took

    def kind(thread):
        return 0 if thread == timeline.main else 1 if thread in logic else 2 if thread in workers else 3

    def order(thread):
        return kind(thread), int(thread) if thread.isdigit() else 0, thread
    names = {thread: ("main", "logic", f"worker {thread}", f"thread {thread}")[kind(thread)] for thread in busy}
    shared = {name for name, count in collections.Counter(names.values()).items() if count > 1}
    names = {thread: f"{name} {thread}" if name in shared else name for thread, name in names.items()}
    return Work({names[thread]: busy[thread] for thread in sorted(busy, key=order)}, zones)


def read_messages(text):
    """(text, ns since the capture began) of each message in `tracy-csvexport -m` output."""
    messages = []
    for line in text.splitlines()[1:]:
        message, _, ns = line.rpartition(EXPORT_SEP)
        if ns.isdigit():
            messages.append((message, int(ns)))
    return messages


def measure_segments(messages, first, last, camera):
    """(label, start ns, end ns) of each part of a run whose frames start between `first` and `last`: a camera path
    run's segments from their messages, each split into its first NEW_GROUND_SECONDS and the rest; else the whole run
    after the scene has loaded."""
    if not camera:
        return [("whole run", first + round(SETTLE_SECONDS * 1e9), last)]
    starts = [(text.removeprefix(SEGMENT_PREFIX), ns) for text, ns in messages if text.startswith(SEGMENT_PREFIX)]
    segments = []
    for (name, start), end in zip(starts, [ns for _, ns in starts[1:]] + [last]):
        cut = min(start + round(NEW_GROUND_SECONDS * 1e9), end)
        segments += [(f"{name}: first {NEW_GROUND_SECONDS:g} s", start, cut), (f"{name}: rest", cut, end)]
    return segments


def segment_frames(timeline, start, end):
    """The frames starting from `start` until `end`."""
    return range(bisect.bisect_left(timeline.starts, start), min(bisect.bisect_left(timeline.starts, end), len(timeline.starts) - 1))


def segment_stats(label, timeline, work, frames, hitches):
    """A segment's numbers over `frames`, with those of the `hitches` frames it holds."""
    rows = [("frame", percentiles([timeline.frame(i) / MS for i in frames])),
            ("work", percentiles([timeline.work(i) / MS for i in frames])),
            *((thread, percentiles([busy[i] / MS for i in frames])) for thread, busy in work.busy.items())]
    gpu = sum(timeline.waits[i] for i in frames) / sum(timeline.frame(i) for i in frames)
    found = [(timeline.starts[i] - timeline.starts[0], timeline.work(i), timeline.frame(i), work.zones[i].most_common(3))
             for i in hitches if i in frames]
    return Segment(label, len(frames), rows, gpu, found)


def input_latencies(log):
    return [float(ms) for ms in LATENCY_RE.findall(log)]


def world_shimmer(output):
    """The world's shimmer the reference pictures' world case prints, if it did."""
    m = SHIMMER_RE.search(output)
    return m and float(m[1])


def display_mode(log):
    """The display the game's window ended on: the log's last display line, as the game logs one again whenever the
    window moves to another display (a compositor may place a new window elsewhere than SDL opened it)."""
    modes = DISPLAY_RE.findall(log)
    return modes[-1] if modes else "unknown"


def untrusted_reasons(log, camera):
    """Why a run's game log says its numbers measure no running game: a pause, a camera path warning, a path that
    didn't finish."""
    reasons = [*PAUSED_RE.findall(log), *PATH_WARNING_RE.findall(log)]
    if camera and "camera path finished" not in log:
        reasons.append("the camera path didn't finish")
    return list(dict.fromkeys(reasons))


def paced(segment, display):
    """Whether the segment's median frame is the display's refresh interval (`display` as `display_mode` reads it):
    the display, not the frame's work or the GPU, set its pace."""
    m = REFRESH_RE.search(display)
    if not m or not float(m[1]):
        return False
    interval = 1000 / float(m[1])
    median = segment.rows[0][1][0]
    return abs(median - interval) <= PACED_TOLERANCE * interval


def measure_run(name, bdir, tools, folder):
    """Run the game as the run `name` says under Tracy, export the capture and read its numbers; its capture, export and
    game log go into `folder`."""
    flags, seconds = MEASURE_RUNS[name]
    recorded, log, export = (folder / f"{name}{suffix}" for suffix in (".tracy", ".log", ".csv"))
    print(f"measure: {name} runs fullscreen for up to {seconds} s; leave its window focused and nothing else animating",
          flush=True)
    # Started as `profile` and `run` start it, on every host; the output goes to a file, never through a filter.
    with log.open("w", encoding="utf-8") as out:
        record(tools["tracy-capture"], ["xmake", "run", APP, *MEASURE_FLAGS, "--quit-after", str(seconds), *flags],
               recorded, env=xmake_env(bdir), stdout=out, stderr=subprocess.STDOUT)
    with export.open("w", encoding="utf-8") as out:
        run([tools["tracy-csvexport"], "-u", "-e", "-s", EXPORT_SEP, recorded], stdout=out)
    messages = capture([tools["tracy-csvexport"], "-m", "-s", EXPORT_SEP, recorded])
    return read_run(name, export, messages, log.read_text(encoding="utf-8", errors="replace"))


def read_run(name, export, messages_text, text):
    """A run's numbers from its zone export (the file `export`), its messages export and its game log."""
    camera = "--camera-path" in MEASURE_RUNS[name][0]
    messages = read_messages(messages_text)
    with export.open(encoding="utf-8") as lines:
        timeline = frame_timeline(lines)
    parts = [(label, frames) for label, start, end in measure_segments(messages, timeline.starts[0], timeline.starts[-1], camera)
             if (frames := segment_frames(timeline, start, end))]
    hitches = [i for _, frames in parts for i in hitch_frames(timeline, frames)]
    with export.open(encoding="utf-8") as lines:
        work = frame_work(lines, timeline, hitches)
    reasons = untrusted_reasons(text, camera) + ([] if parts else ["no frames measured"])
    return MeasuredRun(name, display_mode(text), input_latencies(text), reasons,
                       [segment_stats(label, timeline, work, frames, hitches) for label, frames in parts])


def measure_table(results, shimmer):
    """The table `measure` prints: per run and segment the frame time, its work and each thread's (ms), the GPU's share
    (marked paced when the display set the pace) and the hitches; each run's display, input latency, earlier numbers
    and trust beside its first row; then each hitch with its largest zones. `shimmer` is the world's shimmer as text."""
    rows = []
    for result in results:
        notes = [f"display {result.display}", *(f"input latency {ms:g} ms" for ms in result.latencies),
                 *([f"earlier, unpinned: {MEASURE_EARLIER[result.name]}"] if result.name in MEASURE_EARLIER else []),
                 *([f"untrusted: {'; '.join(result.reasons)}"] if result.reasons else [])]
        first_of_run = True
        for segment in result.segments:
            gpu = f"{segment.gpu:.0%}" + (" paced" if paced(segment, result.display) else "")
            for n, (what, values) in enumerate(segment.rows):
                rows.append((result.name if first_of_run else "", segment.label if n == 0 else "",
                             str(segment.frames) if n == 0 else "", what,
                             *("—" if v is None else f"{v:.2f}" for v in values),
                             gpu if n == 0 else "", str(len(segment.hitches)) if n == 0 else "",
                             "; ".join(notes) if first_of_run else ""))
                first_of_run = False
        if first_of_run:
            rows.append((result.name, "", "", "", "", "", "", "", "", "; ".join(notes)))
    header = ("run", "segment", "frames", "time", "median ms", "p99 ms", "worst ms", "GPU", "hitches", "")
    widths = [max(len(row[c]) for row in (header, *rows)) for c in range(len(header) - 1)]
    numeric = {2, 4, 5, 6, 7, 8}

    def line(row):
        cells = [cell.rjust(width) if c in numeric else cell.ljust(width) for c, (cell, width) in enumerate(zip(row, widths))]
        return "  ".join([*cells, row[-1]]).rstrip()
    out = [f"measure: {MEASURE_WORKERS} workers, unpaced, fullscreen at native size; world shimmer: {shimmer}",
           line(header), *map(line, rows),
           (f"p99 —: fewer than {P99_FRAMES} frames, where it would be the worst. GPU: the share of the frame time spent "
            f"waiting for the next swapchain image ({GPU_WAIT_ZONE}); paced: the median frame is within "
            f"{PACED_TOLERANCE:.0%} of the display's refresh interval, so there the share is the wait to present.")]
    hitches = [(result.name, segment.label, hitch) for result in results for segment in result.segments
               for hitch in segment.hitches]
    out.append(f"hitches (work, a frame's time less its wait for the GPU, over {HITCH_MS} ms): "
               + ("" if hitches else "none"))
    for name, label, (start, took, frame, zones) in hitches:
        largest = ", ".join(f"{zone} {ns / MS:.2f}" for zone, ns in zones)
        out.append(f"  {name}, {label}, at {start / 1e9:.2f} s: work {took / MS:.2f} ms, frame {frame / MS:.2f} ms; "
                   f"largest zones (self time, ms): {largest}")
    return "\n".join(out)


def cmd_measure(args):
    runs = measure_runs(args.runs)
    shimmer = "not measured (no world run)"
    if "world" in runs and HOST != "linux":
        shimmer = "not measured (the reference pictures run on Linux only)"
    elif "world" in runs:
        build(NATIVE_PRESET, "release")
        try:
            value = world_shimmer(run_references(NATIVE_PRESET, quiet=True))
        except (Fail, subprocess.CalledProcessError) as e:  # a picture failing its bound mustn't stop the runs
            print(f"measure: the world's shimmer isn't measured: {getattr(e, 'stderr', None) or e}".rstrip(),
                  file=sys.stderr)
            shimmer = "not measured (reference failed)"
        else:
            shimmer = (f"{value:g} (the reference pictures' world case)" if value is not None
                       else "not measured (the reference pictures printed none)")
    bdir = build(NATIVE_PRESET, "releasedbg")
    tools = {program: profiler_tool(program, package)
             for program, package in (("tracy-capture", "tracy-profiler-capture"), ("tracy-csvexport", "tracy-profiler-csvexport"))}
    folder = ROOT / "build" / "measure" / time.strftime("%Y%m%d-%H%M%S")
    folder.mkdir(parents=True, exist_ok=True)
    try:
        results = [measure_run(name, bdir, tools, folder) for name in runs]
    except BaseException:
        print(f"measure: the runs' captures, exports and game logs are kept in {folder}", file=sys.stderr)
        raise
    untrusted = [result.name for result in results if result.reasons]
    if args.keep or untrusted:  # an untrusted run's game log says why
        print(f"measure: the runs' captures, exports and game logs are kept in {folder}")
    else:
        shutil.rmtree(folder)
    print(measure_table(results, shimmer))
    if untrusted:
        print(f"measure: warning: {len(untrusted)} of {len(results)} runs untrusted: {', '.join(untrusted)}; "
              "run again with the window focused and nothing else animating", file=sys.stderr)
        return 1


def cmd_selftest(args):
    run([sys.executable, "-m", "unittest", "discover", "-q", "-s", "tools"])


def require_current_main(what):
    if capture(["git", "rev-parse", "--abbrev-ref", "HEAD"]).strip() != "main":
        raise Fail(f"{what} runs on main")
    if tree_dirty():
        raise Fail("working tree is not clean")
    run(["git", "fetch", "--quiet"])
    if capture(["git", "rev-list", "--count", "HEAD..@{upstream}"], check=False).strip() not in ("", "0"):
        raise Fail("main is behind its upstream: pull first")


def cmd_release(args):
    if args.tag:
        return tag_release()
    require_current_main("release")
    version = capture([*CLIFF, "--bumped-version"]).strip().removeprefix("v")
    if args.dry_run:
        run([*CLIFF, "--unreleased", "--tag", f"v{version}"])
        print(f"release --dry-run: would release v{version} (current {read_version()})")
        return
    # main takes changes only through pull requests (.github/rulesets/main.json), the release commit too.
    branch = f"release/v{version}"
    if capture(["git", "branch", "--list", branch]).strip():
        raise Fail(f"{branch} already exists, from an earlier attempt at this version: once it is merged or "
                   f"abandoned, delete it (git branch -D {branch}) and run release again")
    run(["git", "switch", "-c", branch])
    xmake_lua = ROOT / "xmake.lua"
    xmake_lua.write_text(with_version(xmake_lua.read_text(encoding="utf-8"), version), encoding="utf-8")
    run([*CLIFF, "--tag", f"v{version}", "-o", "CHANGELOG.md"])
    run(["git", "add", "xmake.lua", "CHANGELOG.md"])
    run(["git", "commit", "-m", f"chore(release): v{version}"])
    print(f"release: committed v{version} on release/v{version}; merge it through a pull request, "
          "then on main: project_n release --tag")


def tag_release():
    """Tags main's merged release commit: a rebase merge rewrites commits, so the tag comes after it."""
    require_current_main("release --tag")
    version = read_version()
    subject = capture(["git", "log", "-1", "--format=%s"]).strip()
    if subject != f"chore(release): v{version}":
        raise Fail(f"main's last commit is '{subject}', not 'chore(release): v{version}': merge the release pull request and pull first")
    # The pull request may have been rebased onto commits that arrived after `release` wrote the changelog.
    # git-cliff dates the new heading today, so headings are compared without their dates.
    undated = functools.partial(re.sub, r"(?m)^(## \S+) - \d{4}-\d{2}-\d{2}$", r"\1")
    if undated(capture([*CLIFF, "--tag", f"v{version}"])) != undated((ROOT / "CHANGELOG.md").read_text(encoding="utf-8")):
        raise Fail(f"CHANGELOG.md no longer matches the commits since the last release (main moved on after `release`): "
                   f"don't tag this commit; delete the merged local release/v{version} branch and run `release` again: it commits the next version, which the newer commits may have changed (check it with --dry-run)")
    run(["git", "tag", "-a", f"v{version}", "-m", f"v{version}"])
    print(f"release: tagged v{version}; push the tag with: git push origin v{version}")


UNSIGNED_NOTE = """These builds aren't code-signed yet. On macOS, open project_n.app once; when macOS refuses, go to
System Settings > Privacy & Security and choose Open Anyway. On Windows, if SmartScreen warns,
choose More info, then Run anyway.

Each `-symbols` zip holds debug symbols for reading crash reports; players don't need it. The source
code of this release is attached below."""


def release_notes(changelog, version):
    """CHANGELOG.md's section for `version`, then what players need to know about these builds."""
    m = re.search(rf"^## {re.escape(version)} [^\n]*\n(.*?)(?=^## |\Z)", changelog, re.MULTILINE | re.DOTALL)
    if not m:
        raise Fail(f"CHANGELOG.md has no section for {version}")
    return f"{m[1].strip()}\n\n{UNSIGNED_NOTE}\n"


def release_archives(version):
    """Every preset's game and symbols archive for `version` in dist/; fails naming any missing."""
    wanted = [ROOT / "dist" / f"{APP}-{version}-{preset}{part}.zip" for preset in PRESETS for part in ("", "-symbols")]
    missing = [a.name for a in wanted if not a.is_file()]
    if missing:
        raise Fail("missing release archives:\n" + "\n".join(missing))
    return wanted


def cmd_publish(args):
    version = read_version()
    tag = f"v{version}"
    if tag not in capture(["git", "tag", "--points-at", "HEAD"]).split():
        raise Fail(f"publish runs on the commit tagged {tag}")
    archives = release_archives(version)
    notes = ROOT / "build" / "release-notes.md"
    notes.parent.mkdir(exist_ok=True)
    notes.write_text(release_notes((ROOT / "CHANGELOG.md").read_text(encoding="utf-8"), version), encoding="utf-8")
    run(["gh", "release", "create", tag, "--verify-tag", "--draft", "--title", f"{APP} {version}", "--notes-file", notes, *archives])


def cmd_commit_check(args):
    if args.range:
        log = capture(["git", "log", "--format=%H%x00%B%x01", args.range])
        commits = [entry.strip("\n").split("\0", 1) for entry in log.split("\1") if entry.strip()]
        failed = 0
        for sha, message in commits:
            for problem in check_message(message, allow_fixup=False):
                print(f"{sha[:10]}: {problem}")
                failed = 1
        if not failed:
            print(f"commit-check: {len(commits)} commit(s) OK")
        return failed
    problems = check_message(Path(args.message_file).read_text(encoding="utf-8"))
    for problem in problems:
        print(f"commit-check: {problem}", file=sys.stderr)
    if problems:
        print("See the git-commit skill.", file=sys.stderr)
    return 1 if problems else 0


def check_message(message, allow_fixup=True):
    """Return a list of problems with one commit message (empty when valid)."""
    lines = [line for line in message.splitlines() if not line.startswith("#")]
    while lines and not lines[0].strip():
        lines.pop(0)
    if not lines:
        return ["empty commit message"]
    header = lines[0]
    if header.startswith(("fixup! ", "squash! ", "amend! ")):
        return [] if allow_fixup else ["fixup/squash commits must be squashed before merging"]
    if header.startswith("Merge "):
        return ["merge commits are not allowed: history is linear (rebase instead)"]

    problems = []
    m = HEADER_RE.match(header)
    if not m:
        return [f"header must be '<type>[(<scope>)][!]: <description>', got: {header!r}"]
    if m["type"] not in TYPES:
        problems.append(f"unknown type {m['type']!r}; use one of: {', '.join(TYPES)}")
    if m["scope"] and m["scope"] not in SCOPES:
        problems.append(f"unknown scope {m['scope']!r}; add it to SCOPES in tools/project_n.py or use one of: {', '.join(SCOPES)}")
    desc = m["desc"]
    if desc[0].isupper():
        problems.append("description must start with a lowercase letter")
    if desc.endswith("."):
        problems.append("description must not end with a period")
    if len(header) > MAX_HEADER:
        problems.append(f"header is {len(header)} characters; the limit is {MAX_HEADER}")
    if len(lines) > 1 and lines[1].strip():
        problems.append("a blank line must separate the header from the body")
    return problems


# A quoted command, not prose that names the project ("a project_n branch").
DOC_COMMAND_RE = re.compile(r"(?:pixi run project_n|`project_n|\./dev) +([a-z][a-z-]*)")


def stale_doc_commands():
    """`project_n <cmd>` and `./dev <cmd>` quoted in AGENTS.md, CLAUDE.md, skills, agents and README that don't exist."""
    commands = set(make_parser()._subparsers._group_actions[0].choices)
    # Ours only (tracked or new): build/ holds xmake-repo's own AGENTS.md.
    docs = capture(["git", "ls-files", "--cached", "--others", "--exclude-standard", "--",
                    "*AGENTS.md", "*CLAUDE.md", ".claude/skills/*.md", ".claude/agents/*.md", "README.md"]).splitlines()
    stale = []
    for doc in [d for d in docs if (ROOT / d).is_file()]:  # is_file: deleted but not yet committed
        for n, line in enumerate((ROOT / doc).read_text(encoding="utf-8").splitlines(), 1):
            for cmd in DOC_COMMAND_RE.findall(line):
                if cmd not in commands:
                    stale.append(f"{doc}:{n}: unknown command {cmd!r}")
    return stale


def cmd_doctor(args):
    if args.docs:
        stale = stale_doc_commands()
        print("\n".join(stale) or "doctor --docs: every quoted command exists")
        return 1 if stale else 0
    problems = 0

    def report(ok, what, fix=None):
        nonlocal problems
        print(f"{'ok ' if ok else 'BAD'}  {what}")
        if not ok:
            problems += 1
            if fix:
                print(f"     fix: {fix}")

    report(sys.version_info >= (3, 11), f"python {sys.version.split()[0]} (>= 3.11)")
    report("CONDA_PREFIX" in os.environ, "running inside the Pixi environment", "run through: pixi run project_n doctor")
    pinned = pinned_xmake()
    xmake = capture(["xmake", "--version"], check=False) if shutil.which("xmake") else ""
    report(pinned in xmake, f"xmake {pinned} from the Pixi env", "pixi install")
    report(clone_set_up(), "clone set up (commit hooks, Git LFS filters through Pixi)", "pixi run project_n setup")
    missing = lfs_missing()
    report(not missing, "Git LFS files downloaded" + (f" (missing: {', '.join(missing)})" if missing else ""),
           "pixi run project_n setup")
    podman = shutil.which("podman")
    report(bool(podman), "podman (artefacts, container checks" + (", windows-msvc)" if HOST == "linux" else ")"),
           INSTALL_HINTS["podman"][HOST])
    if podman:
        for name in IMAGES:
            tag = image_tag(name)
            built = subprocess.run(["podman", "image", "exists", tag], check=False).returncode == 0
            print(f"{'ok ' if built else '-- '}  image {tag}" + ("" if built else " (built on first use)"))
        installed = (msvc_dir() / ".installed").exists()
        print(f"{'ok ' if installed else '-- '}  MSVC in {msvc_dir()}" + ("" if installed else " (installed on first use)"))
    if HOST == "windows":
        vswhere = Path(os.environ.get("ProgramFiles(x86)", ""), "Microsoft Visual Studio/Installer/vswhere.exe")
        installs = capture([vswhere, "-products", "*", "-property", "installationPath"], check=False).splitlines() if vswhere.exists() else []
        # Look at the directories: vswhere -find misses a toolset that wasn't installed by the VS installer (CI's cache).
        found = any(Path(vs, "VC", "Tools", "MSVC").glob(f"{VS_TOOLSET}.*") for vs in installs)
        report(found, f"MSVC toolset {VS_TOOLSET} (Build Tools)", INSTALL_HINTS["vs"]["windows"])
        if podman and not str(ROOT).startswith("\\\\"):  # every drive letter is NTFS; WSL paths are \\wsl$\...
            print("note  container builds from an NTFS checkout are slow; keep a clone inside WSL for them")
    if HOST == "macos":
        sdk = capture(["xcrun", "--show-sdk-version"], check=False).strip() if shutil.which("xcrun") else ""
        report(bool(sdk), f"Xcode Command Line Tools (macOS SDK {sdk or 'missing'})", INSTALL_HINTS["xcode"]["macos"])
    if os.environ.get("PROJECT_N_ACCEPT_MSVC_LICENSE") != "1":
        print("--   PROJECT_N_ACCEPT_MSVC_LICENSE is not set: the MSVC image can't be built until you accept its licence")
    return DOCTOR_PROBLEMS if problems else 0


def cmd_image(args):
    for preset in [args.preset] if args.preset else [p for p in PRESETS if PRESETS[p]["image"]]:
        image, mounts = preset_container(preset)
        print(image, *mounts)


def cmd_shell(args):
    image, mounts = preset_container(args.preset)
    run(container_cmd(image, ["bash"], interactive=True, mounts=mounts))


def cmd_clean(args):
    if args.stale:
        if args.preset or args.packages or args.images:
            raise Fail("clean --stale sweeps every preset's trees and nothing else: pass --preset, --packages or "
                       "--images without it")
        sweep_build(ROOT / "build", time.time(), days=STALE_DAYS if args.days is None else args.days)
        return
    if args.days is not None:
        raise Fail("--days goes with --stale")
    for preset in [args.preset] if args.preset else PRESETS:
        for d in (ROOT / "build" / preset, ROOT / "build" / "container" / preset):
            shutil.rmtree(d, ignore_errors=True)
    if args.images:
        current = {image_tag(name).split(":")[1] for name in IMAGES}
        listed = capture(["podman", "images", "--format", "{{.Repository}}:{{.Tag}}"]).split()
        # Only this project's images (any registry), and only stale tags of them.
        stale = [ref for ref in listed if re.search(r"/project_n-[a-z]+:", ref) and ref.rsplit(":", 1)[1] not in current]
        if stale:
            run(["podman", "rmi", *stale])
    if args.packages:
        current = packages_dir().parent
        for d in current.parent.iterdir() if current.parent.exists() else ():
            if d != current:
                print(f"removing {d}")
                shutil.rmtree(d)


# Where worktrees of this project are made: superpowers' fallback and Claude Code's own.
WORKTREE_DIRS = (".worktrees", ".claude/worktrees")


def main_checkout():
    return Path(capture(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"]).strip()).parent


def owned_worktrees(porcelain, main):
    """The worktrees in `git worktree list --porcelain` output that this project made: those under WORKTREE_DIRS."""
    owned = []
    for block in porcelain.strip().split("\n\n"):
        fields = dict((line.split(" ", 1) + [""])[:2] for line in block.splitlines())
        path = Path(fields["worktree"])
        if any(path.is_relative_to(main / d) for d in WORKTREE_DIRS):
            branch = fields.get("branch", "").removeprefix("refs/heads/") or None
            owned.append({"path": path, "branch": branch, "locked": "locked" in fields, "prunable": "prunable" in fields})
    return owned


def worktree_unremovable(worktree):
    """Why `worktree` must stay, or None once its work is in origin/main. A rebase merge rewrites the commits, so
    "merged" means GitHub deleted the branch and every commit's change is in origin/main (`git cherry`)."""
    if worktree["path"] == ROOT:
        return "this checkout (run it from another)"
    if worktree["locked"]:
        return f"locked (a session is using it; once none is, git worktree unlock {worktree['path']})"
    if not worktree["branch"]:
        return "no branch (detached)"
    if capture(["git", "-C", worktree["path"], "status", "--porcelain"]).strip():
        return "uncommitted changes"
    return branch_unmerged(worktree["branch"])


def branch_unmerged(branch):
    """Why `branch`'s work isn't in origin/main yet, or None. A rebase merge rewrites the commits, so "merged" means
    GitHub deleted the branch and every commit's change is in origin/main (`git cherry`)."""
    if capture(["git", "ls-remote", "--heads", "origin", branch]).strip():
        return f"{branch} is still on origin (its pull request isn't merged)"
    cherry = capture(["git", "cherry", "origin/main", branch]).splitlines()
    if not cherry:  # never pushed either: a worktree just made, whose untracked build/ and session notes would go
        return f"{branch} has no commits of its own yet"
    if any(line.startswith("+") for line in cherry):
        return f"{branch} has commits not in origin/main"
    return None


def prune_worktrees(args):
    """Remove this project's worktrees whose work is merged, with their branches; never forced."""
    main = main_checkout()
    run(["git", "fetch", "--quiet", "--prune", "origin"])
    merged_gone = []
    for worktree in owned_worktrees(capture(["git", "worktree", "list", "--porcelain"]), main):
        if worktree["prunable"]:  # its folder is gone: the `git worktree prune` below clears it, then its merged branch
            reason = branch_unmerged(worktree["branch"]) if worktree["branch"] else "no branch (detached)"
            print(f"pruning {worktree['path']}: folder deleted" + (f"; keeping its branch: {reason}" if reason else ""))
            if not reason:
                merged_gone.append(worktree["branch"])
            continue
        reason = worktree_unremovable(worktree)
        if reason:
            print(f"keeping {worktree['path']}: {reason}")
            continue
        run(["git", "worktree", "remove", worktree["path"]])
        run(["git", "branch", "-D", worktree["branch"]])  # -D: a rebase merge leaves it unmerged by ancestry
    run(["git", "worktree", "prune"])
    for branch in merged_gone:  # only once pruned: git won't delete a branch a registered worktree has checked out
        run(["git", "branch", "-D", branch])


def ensure_macos_xmake():
    """Download the pinned XMake bundle once per version and put it first on PATH."""
    version = pinned_xmake()
    exe = Path.home() / ".xmake" / "project_n-tools" / f"xmake-{version}" / "bin" / "xmake"  # outside clean --packages' reach
    if not exe.exists():
        url = f"https://github.com/xmake-io/xmake/releases/download/v{version}/xmake-bundle-v{version}.macos.arm64"
        print(f"+ download {url}", flush=True)
        data = urllib.request.urlopen(url, timeout=60).read()
        if hashlib.sha256(data).hexdigest() != XMAKE_MACOS_SHA256:
            raise Fail(f"checksum mismatch for {url}: update XMAKE_MACOS_SHA256 only on purpose (bump-toolchain skill)")
        exe.parent.mkdir(parents=True, exist_ok=True)
        part = exe.with_suffix(".part")  # renamed into place: an interrupted download never looks installed
        part.write_bytes(data)
        part.chmod(0o755)
        part.replace(exe)
    os.environ["PATH"] = f"{exe.parent}{os.pathsep}{os.environ['PATH']}"


def help_format(prog):
    """Piped help isn't wrapped: shell completers (lig) read an option's values from the first line of its help."""
    return argparse.HelpFormatter(prog, width=None if sys.stdout.isatty() else 10_000)


def make_parser():
    parser = argparse.ArgumentParser(prog="project_n", description=__doc__.splitlines()[0], formatter_class=help_format)
    sub = parser.add_subparsers(dest="command", required=True, title="commands", metavar="<command>")

    def add(name, func, help, preset=None, mode=False, container=False, choices=tuple(PRESETS)):
        p = sub.add_parser(name, help=help, formatter_class=help_format)
        if preset is not None:
            p.add_argument("--preset", choices=choices, required=preset == "required",
                           default=NATIVE_PRESET if preset == "native" else None)
        if mode:
            p.add_argument("--mode", choices=MODES, default="debug")
        if container:
            p.add_argument("--container", action="store_true", help="force the preset's container")
        p.set_defaults(func=func)
        return p

    all_sanitizers = sorted({s for supported in SANITIZERS.values() for s in supported})

    def add_sanitizer(p):
        p.add_argument("--sanitizer", choices=["none", *all_sanitizers], default="none",
                       help="build with a sanitizer (support per preset: see SANITIZERS)")

    def add_scope(p):
        p.add_argument("--all", action="store_true", help="cover every source, not only the diff")
        p.add_argument("--range", help="diff against this commit range's base (default: a branch's commits since main, or main's unpushed ones)")

    add("setup", cmd_setup, "set up a clone once: the commit hooks and Git LFS's filters (local config), then fetch LFS files")
    add("lfs-key", cmd_lfs_key, "print the cache key of this commit's Git LFS objects (CI's LFS action)")
    add("lfs-prune", cmd_lfs_prune, "delete Git LFS objects the checkout doesn't use, before CI caches them (frees disk locally too)")
    add("prune-build-cache", cmd_prune_build_cache,
        "keep only the objects and clang-tidy results this run used in PROJECT_N_BUILD_CACHE, before CI saves it")
    add("prune-ci-caches", cmd_prune_ci_caches,
        "delete this ref's superseded Actions cache entries, keeping each family's newest (CI's cleanup job)")
    p = add("doctor", cmd_doctor, "check the environment; prints how to fix problems, installs nothing")
    p.add_argument("--docs", action="store_true", help="only check that commands quoted in agent docs and README exist")
    add_sanitizer(add("build", cmd_build, "build a preset, or all of them", preset="native", mode=True, container=True,
                      choices=(*PRESETS, "all")))
    p = add("test", cmd_test, "build and run the tests of a preset", preset="native", mode=True, container=True)
    add_sanitizer(p)
    suites = test_names()  # in the help only, not `choices`: any target and any case name are valid
    test_files = all_test_files(suites)
    p.add_argument("--suite", "--target", dest="target",
                   help=f"[possible values: {', '.join(suites)}] run one test target, forwarding the arguments after --")
    p.add_argument("--test", help=f"[possible values: {', '.join(test_files)}] with --suite: only the cases of a test file, or with this in their name")
    p.add_argument("--list-tests", action="store_true", help="print what --test takes (of --suite's files, or of every suite's) and exit: shell completion calls it")
    p.add_argument("--unity", action="store_true", help="with unity builds on (off by default; check runs this once)")
    p.add_argument("args", nargs=argparse.REMAINDER, help="-- <doctest arguments>, e.g. -- --test-case='*physics*'")
    p = add("run", cmd_run, "build and launch the game natively", preset="native", mode=True)
    p.add_argument("--debug", action="store_true", help="launch under the debugger (xmake run -d)")
    add_sanitizer(p)
    p.add_argument("args", nargs=argparse.REMAINDER, help="-- <game arguments>, e.g. -- --stress")
    p = add("check", cmd_check, "the merge gate: commits, format and lint of the diff, build and test every preset", container=True)
    p.add_argument("--sanitizers", action="store_true", help="also test every preset under each supported sanitizer")
    p = add("artifact", cmd_artifact, "clean-room release build in the preset's container, into dist/", preset="required")
    p.add_argument("--reuse-build", action="store_true",
                   help="package the release build an earlier `test --mode release` left instead of a clean room; refused on CI's main and tags, which build from source")
    p = add("release", cmd_release, "bump the version with git-cliff, write CHANGELOG.md and commit it on a release/ branch")
    group = p.add_mutually_exclusive_group()
    group.add_argument("--dry-run", action="store_true")
    group.add_argument("--tag", action="store_true", help="after the release pull request merged: tag main's release commit")
    add("publish", cmd_publish, "draft the tagged version's GitHub release with every preset's archives from dist/ (CI runs it)")
    p = add("commit-check", cmd_commit_check, "validate commit messages (Conventional Commits)")
    group = p.add_mutually_exclusive_group(required=True)
    group.add_argument("message_file", nargs="?", help="a commit message file (used by the commit-msg hook)")
    group.add_argument("--range", help="validate every commit in a git range, e.g. main..HEAD")
    add("coverage", cmd_coverage, "test coverage of src/ (linux-gcc, gcovr): build/coverage/index.html", container=True)
    p = add("fuzz", cmd_fuzz, "fuzz a libFuzzer target from tests/fuzz/ (linux-clang, AddressSanitizer)", container=True)
    p.add_argument("target", choices=sorted(f.stem for f in (ROOT / "tests" / "fuzz").glob("*.cpp")))
    p.add_argument("--time", type=int, default=60, help="seconds to fuzz (default 60)")
    add("docs", cmd_docs, "API reference from doc comments with Doxygen: build/docs/html/")
    p = add("reference", cmd_reference, "compare pictures of fixed scenes with tests/reference/ on software Vulkan (Linux)",
            preset="native", container=True)
    p.add_argument("--update", action="store_true", help="take this run's pictures as the new references")
    p = add("profiler", cmd_profiler, "start the Tracy viewer, live on a `run --mode releasedbg` build or on a saved capture")
    p.add_argument("file", nargs="?", help="a capture to open, e.g. one `profile` saved")
    p = add("profile", cmd_profile, "record a releasedbg run with Tracy into build/profiles/, without a click that pauses the game",
            preset="native")
    p.add_argument("--seconds", type=int, default=10, help="how long the game runs before it quits (default 10)")
    p.add_argument("args", nargs=argparse.REMAINDER, help="-- <game arguments>, e.g. -- --stress")
    p = add("measure", cmd_measure,
            f"the smoothness and cost numbers: releasedbg runs fullscreen under Tracy, {MEASURE_WORKERS} workers, unpaced, "
            "as one table; exits 1 if a run paused (never a gate)")
    p.add_argument("--runs", help=f"[possible values: {', '.join(MEASURE_RUNS)}] the runs, comma-separated (default: all)")
    p.add_argument("--keep", action="store_true", help="keep each run's capture, export and game log in build/measure/")
    p = add("atlas", cmd_atlas, "assign the sprites of art/sprites.toml their texture-array pages (src/core/generated/atlas.hpp)")
    p.add_argument("--placeholders", action="store_true", help="first redraw the procedural placeholders (every image in PLACEHOLDERS in art/placeholders.py)")
    add("shaders", cmd_shaders, "compile shaders/*.hlsl into shaders/bin/ (SPIR-V, DXIL, MSL); Linux and Windows only")
    add("selftest", cmd_selftest, "run project_n.py's own unit tests (tools/test_project_n.py)")
    p = add("fmt", cmd_fmt, "format changed sources with the pinned clang-format (--all: every source)")
    p.add_argument("--check", action="store_true", help="fail instead of rewriting files")
    p.add_argument("--staged", action="store_true", help="check what is staged, as committed (the pre-commit hook)")
    add_scope(p)
    p = add("lint", cmd_lint, "run the pinned clang-tidy over changed sources (--all: every source); any finding fails",
            container=True)
    p.add_argument("--fix", action="store_true", help="apply clang-tidy's fixes, then reformat")
    add_scope(p)
    add("image", cmd_image, "build or pull container images if missing, and install MSVC for windows-msvc", preset="optional")
    add("shell", cmd_shell, "open a shell in a preset's container (to debug container failures)", preset="required")
    p = add("clean", cmd_clean, "remove build directories", preset="optional")
    p.add_argument("--stale", action="store_true",
                   help="only remove build trees unused for --days, but the native preset's debug and release (as build, test and check do)")
    p.add_argument("--days", type=int, help=f"with --stale: the age in days (default {STALE_DAYS})")
    p.add_argument("--packages", action="store_true", help="also remove XMake package dirs of other environments")
    p.add_argument("--images", action="store_true", help="also remove this project's container images other than the current tags")
    add("prune-worktrees", prune_worktrees,
        "remove this project's worktrees (.worktrees/, .claude/worktrees/) whose work is merged into origin/main, with their branches")
    # List choices in clap's `[possible values: …]` form, which shell completers (lig) read from --help.
    # First, so argparse's wrapping can't split the marker across lines.
    for p in sub.choices.values():
        for a in p._actions:
            if a.choices and a.option_strings:
                a.help = f"[possible values: {', '.join(map(str, a.choices))}] {a.help or ''}".rstrip()
    return parser


def main(argv=None):
    args = make_parser().parse_args(argv)
    try:
        if HOST == "macos" and args.func not in (cmd_setup, cmd_lfs_key, cmd_lfs_prune, cmd_prune_build_cache, cmd_prune_ci_caches, cmd_commit_check, cmd_selftest, cmd_fmt, cmd_release, cmd_docs, cmd_profiler, cmd_shaders, cmd_atlas, prune_worktrees):
            ensure_macos_xmake()  # not for commands without XMake: the commit hook must work offline
        return args.func(args) or 0
    except Fail as e:
        print(f"project_n: {e}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as e:
        if e.stderr:
            print(e.stderr, end="", file=sys.stderr)
        return e.returncode or 1


if __name__ == "__main__":
    sys.exit(main())
