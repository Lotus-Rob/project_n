"""project_n: the single build entry point. Run as `pixi run project_n <command>`.

Thin by design: every command reduces to calls of XMake, Podman, git, git-cliff, gh and the pinned tools.
Standard library only. See tools/AGENTS.md and docs/design/0001.
"""

import argparse
import functools
import hashlib
import json
import os
import plistlib
import re
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HOST = "windows" if os.name == "nt" else "macos" if sys.platform == "darwin" else "linux"
IN_CONTAINER = os.environ.get("PROJECT_N_IN_CONTAINER") == "1"

APP = "project_n"
BUNDLE_ID = "dev.projectn.project-n"  # shortcut: placeholder macOS bundle id, fix it for good before signing (O6)
MODES = ("debug", "release", "releasedbg")
VS_TOOLSET = "14.50"  # native Windows MSVC; must match containers/windows/buildtools.vsconfig
# conda-forge has no osx-arm64 XMake, so macOS uses the upstream bundle of set_xmakever()'s
# version (docs/design/0001 §5.7). Checksum of xmake-bundle-v<version>.macos.arm64.
XMAKE_MACOS_SHA256 = "e719121c56cd34f5f0c0731954a154c1df1b0915f303e5c82dfb6c6cb9e3ef9e"
LINT_PRESET = "linux-clang"  # shortcut: #ifdef'd Windows/macOS code isn't linted; lint each native preset once some exists

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

# Single source of truth for presets (docs/design/0001 §3). `host` is the OS that builds natively.
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
IMAGE_INPUTS = ("pixi.toml", "pixi.lock", "tools/env/activate.sh")
IMAGE_IGNORE = "containers/.containerignore"  # the build context is the repo root; this admits only IMAGE_INPUTS
CACHE_VOLUME = "project_n-xmake-cache"  # PROJECT_N_XMAKE_CACHE=<dir> bind-mounts a directory instead (CI caches it)

# Conventional Commits (docs/design/0001 §11.1). Add a scope in the commit that first uses it.
TYPES = ("feat", "fix", "perf", "refactor", "test", "build", "ci", "docs", "style", "chore", "revert")
SCOPES = ("repo", "env", "containers", "tools", "xmake", "deps", "tests", "design", "agents", "release", "core")

HEADER_RE = re.compile(r"^(?P<type>[a-z]+)(?:\((?P<scope>[a-z0-9-]+)\))?(?P<breaking>!)?: (?P<desc>.+)$")
MAX_HEADER = 72
VERSION_RE = re.compile(r'set_version\("([^"]+)"\)')
XMAKEVER_RE = re.compile(r'set_xmakever\("([^"]+)"\)')
CLIFF = ["git", "cliff", "--config", "tools/cliff.toml"]
# --lsp=clangd keeps packages' -isystem: otherwise XMake writes -I, and clangd and clang-tidy then
# report warnings in third-party headers and in their macros as if they were ours.
COMPDB = ["xmake", "project", "-k", "compile_commands", "--lsp=clangd"]

# Host prerequisites (docs/design/0001 §5.0): printed by `doctor`, never installed by it.
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


# --- helpers -----------------------------------------------------------------


def resolve(cmd):
    """Look the program up on PATH: Windows' CreateProcess only finds .exe files."""
    exe = shutil.which(str(cmd[0])) or str(cmd[0])
    if HOST == "windows" and cmd[0] == "xmake" and "CONDA_PREFIX" in os.environ:
        # conda's xmake.cmd ends with `endlocal`, which replaces xmake's exit code with 0: failed
        # builds and tests looked green. Run the xmake.exe it wraps.
        exe = str(Path(os.environ["CONDA_PREFIX"], "Library", "share", "xmake", "xmake.exe"))
    return [exe, *(str(c) for c in cmd[1:])]


def run(cmd, env=None, **kwargs):
    """Run a command from the repo root, echoing it; raise on failure. Output is never piped."""
    print("+", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.run(resolve(cmd), cwd=ROOT, check=True, env=env, **kwargs)


def capture(cmd, check=True):
    # UTF-8, not the locale: Windows would decode with cp1252 and fail on XMake's output.
    return subprocess.run(resolve(cmd), cwd=ROOT, check=check, capture_output=True, encoding="utf-8", errors="replace").stdout


def sha256(*paths):
    h = hashlib.sha256()
    for p in paths:
        h.update(Path(ROOT, p).read_bytes())
    return h.hexdigest()


def needs_container(preset, force=False):
    """The location rule (§2 D9): native when this host builds the preset, else its container."""
    if IN_CONTAINER:
        return False
    return force or PRESETS[preset]["host"] != HOST


def buildable_here(preset):
    """Natively, or through its container; macos-clang has none."""
    return bool(PRESETS[preset]["image"]) or (PRESETS[preset]["host"] == HOST and not IN_CONTAINER)


def build_dir(preset, mode, sanitizer="none", unity=False):
    leaf = (mode if sanitizer == "none" else f"{mode}-{sanitizer}") + ("-unity" if unity else "")
    return Path("build", "container", preset, leaf) if IN_CONTAINER else Path("build", preset, leaf)


def packages_dir():
    """XMake keys its package cache on the toolchain name, not the compiler version, so each
    environment gets its own: a toolchain bump never reuses stale builds. pixi.toml counts too,
    because activation variables (build flags, pkg-config paths) aren't in the lock."""
    return Path.home() / ".xmake" / "project_n" / sha256("pixi.toml", "pixi.lock")[:12] / "packages"


def xmake_env(bdir):
    # Per-build config dir: native and container builds of one preset never share XMake state.
    # The user's own sanitizer options go last: the last value of an option wins.
    sanitizer_env = {k: f"{v}:{os.environ[k]}" if os.environ.get(k) else v for k, v in SANITIZER_ENV.items()}
    return dict(os.environ, XMAKE_CONFIGDIR=str(ROOT / bdir / ".xmake"), XMAKE_PKG_INSTALLDIR=str(packages_dir()),
                **sanitizer_env)


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


# --- containers ----------------------------------------------------------------


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
    # The install also holds an empty servicing stub of Microsoft's newest toolset (14.51), and
    # XMake (--vs_toolset is Windows-only) takes the newest folder as the toolset. Keep real ones.
    for toolset in (dest / "VC" / "Tools" / "MSVC").glob("*"):
        if not (toolset / "bin").is_dir():
            shutil.rmtree(toolset)
    return dest


def preset_container(preset):
    """The preset's image and mounts: windows-msvc also gets the MSVC install, never part of an image."""
    if not PRESETS[preset]["image"]:
        raise Fail(f"{preset} builds only on a {PRESETS[preset]['host']} host; CI's {PRESETS[preset]['host']} job covers it")
    image = ensure_image(PRESETS[preset]["image"])
    return image, [f"{ensure_msvc(image)}:/opt/msvc:Z"] if PRESETS[preset]["toolchain"] == "msvc" else []


def container_cmd(image, inner, interactive=False, mounts=()):
    cache = os.environ.get("PROJECT_N_XMAKE_CACHE")
    if cache:
        Path(cache).mkdir(parents=True, exist_ok=True)
    cmd = ["podman", "run", "--rm", "--userns=keep-id:uid=1000,gid=1000", "-v", f"{ROOT}:/src:Z",
           "-v", f"{cache}:/home/dev/.xmake:Z" if cache else f"{CACHE_VOLUME}:/home/dev/.xmake", "-e", f"PROJECT_N_IMAGE={image}"]
    for mount in mounts:
        cmd += ["-v", mount]
    if interactive:
        cmd.append("-it")
    return cmd + [image, *inner]


def reenter(preset, argv, interactive=False):
    """Run `project_n <argv>` inside the preset's container: one code path, another environment."""
    image, mounts = preset_container(preset)
    run(container_cmd(image, ["pixi", "run", "--quiet", "project_n", *argv], interactive, mounts))


# --- xmake ---------------------------------------------------------------------


def configure(preset, mode, sanitizer="none", unity=False):
    check_sanitizer(preset, sanitizer)
    p, bdir = PRESETS[preset], build_dir(preset, mode, sanitizer, unity)
    # -v makes XMake install packages one at a time (else 4 in parallel). shortcut: XMake installs
    # duplicate variants with the same build hash (libxcb, libx11 for SDL3) concurrently into one
    # directory and they corrupt each other; drop -v once that's fixed upstream.
    cmd = ["xmake", "f", "-y", "-v", "-p", p["plat"], "-a", p["arch"], f"--toolchain={p['toolchain']}", "-m", mode,
           "-o", bdir, f"--sanitizer={sanitizer}", *(["--unity_build=y"] if unity else [])]
    if p["toolchain"] == "msvc":
        cmd.append("--sdk=/opt/msvc" if IN_CONTAINER else f"--vs_toolset={VS_TOOLSET}")
    run(cmd, env=xmake_env(bdir))
    return bdir


def build(preset, mode, sanitizer="none", unity=False):
    bdir = configure(preset, mode, sanitizer, unity)
    run(["xmake", "build"], env=xmake_env(bdir))
    if not IN_CONTAINER and sanitizer == "none" and not unity:  # unity's generated sources would confuse clangd
        # Native builds keep clangd current (.clangd reads build/; container paths would be wrong).
        run([*COMPDB, ROOT / "build"], env=xmake_env(bdir))
    return bdir


def binary_path(preset, mode):
    p = PRESETS[preset]
    exe = APP + (".exe" if p["plat"] == "windows" else "")
    return build_dir(preset, mode) / p["plat"] / p["arch"] / mode / exe


# --- commands ------------------------------------------------------------------


def cmd_build(args):
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


def cmd_test(args):
    extra = args.args[1:] if args.args[:1] == ["--"] else args.args
    if extra and not args.target:
        raise Fail("pass --target <test target> to forward doctest arguments")
    check_sanitizer(args.preset, args.sanitizer)
    if needs_container(args.preset, args.container):
        argv = ["test", "--preset", args.preset, "--mode", args.mode, "--sanitizer", args.sanitizer, *(["--unity"] if args.unity else [])]
        argv += (["--target", args.target] if args.target else []) + (["--", *extra] if extra else [])
        return reenter(args.preset, argv)
    bdir = build(args.preset, args.mode, args.sanitizer, args.unity)
    if PRESETS[args.preset]["plat"] == "windows" and args.sanitizer != "none" and HOST != "windows":
        # MSVC's ASan hot-patches system DLLs; Wine's (CET endbr64 prologues) can't be patched.
        print(f"test: built with {args.sanitizer} sanitizer; MSVC sanitizer runs need native Windows, not Wine")
        return
    if args.target:
        run(["xmake", "run", args.target, *extra], env=xmake_env(bdir))
    else:
        run(["xmake", "test", "--output-on-failure"], env=xmake_env(bdir))


def cmd_run(args):
    if PRESETS[args.preset]["host"] != HOST:
        raise Fail(f"`run` is native only; {args.preset} needs a {PRESETS[args.preset]['host']} host")
    bdir = build(args.preset, args.mode, args.sanitizer)
    run(["xmake", "run", *(["-d"] if args.debug else []), APP], env=xmake_env(bdir))


def is_source(path):
    return path.startswith(("src/", "tests/")) and path.endswith((".cpp", ".hpp"))


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
FACADES = {"src/core/log.hpp": "quill/", "src/core/profile.hpp": "tracy/", "src/core/ecs.hpp": "entt/"}
# The src/ modules (design doc 0002 §4) and the modules each one's code may include: those below it.
MODULES = {"core": (), "data": ("core",), "platform": ("core",), "gfx": ("core", "platform"),
           "ui": ("core", "gfx", "platform"), "audio": ("core",), "game": ("core",)}


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
    (FACADES) may include its own library, and a module's internal/ headers its libraries."""
    found = []
    for header in [f for f in sources if f.startswith("src/") and f.endswith(".hpp")]:
        for n, line in enumerate((ROOT / header).read_text(encoding="utf-8").splitlines(), 1):
            m = INCLUDE_RE.match(line)
            if not m:
                continue
            bracket, name = m.groups()
            standard = is_standard(bracket, name)
            ours = bracket == '"' and (((ROOT / header).parent / name).is_file() or (ROOT / "src" / name).is_file())
            facade = header in FACADES and name.startswith(FACADES[header])
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
    simulation uses core/math.hpp's, which give the same results on every machine (design doc 0002 §7.4)."""
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
    """Lines in src/ that go around a facade: all output goes through PN_LOG_*, and Quill, Tracy and
    EnTT are included only by their facade (FACADES) and its .cpp, so each library is used the same
    way everywhere."""
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


def tidy_verdict(output, returncode):
    """Whether a clang-tidy run fails lint, and how many listed findings (IGNORED_FINDINGS) it
    ignored. clang-tidy keeps an analyzer finding located in a library when its path starts in our
    code: usually our misuse, which fails; only the listed ones don't. A crash always fails."""
    findings = TIDY_FINDING_RE.findall(output)
    ignored = [f for f in findings if any(f[0].endswith(path) and f[1].split(",")[0] == check for path, check in IGNORED_FINDINGS)]
    failed = len(ignored) < len(findings) or returncode not in (0, 1) or (returncode == 1 and not findings)
    return failed, len(ignored)


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


def lint_units(sources):
    """The .cpp files clang-tidy runs on: a header's findings surface through the files that include
    it, so a changed header lints them all."""
    header_changed = any(f.endswith(".hpp") for f in sources)
    return [f for f in source_files() if f.endswith(".cpp") and (header_changed or f in sources)]


def hooks_enabled():
    return capture(["git", "config", "core.hooksPath"], check=False).strip() == ".githooks"


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
                   "stay inside it (design doc 0002 §4, MODULES in tools/project_n.py):\n" + "\n".join(crossed))
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
        raise Fail("the simulation uses core::math (core/math.hpp), not the platform's maths functions (design doc 0002 §7.4):\n"
                   + "\n".join(maths))
    check_api_docs(sources)
    if changed is None or any(f.startswith("tools/") and f.endswith(".py") for f in changed):
        run(["ruff", "check", "tools"])
    if changed is None or any(f.startswith(".github/") for f in changed):
        run(["actionlint"])
    units = lint_units(sources)
    if not units:
        print("lint: no changed C++ sources (--all covers every file)")
        return
    bdir = configure(LINT_PRESET, "debug")
    run(["xmake", "build"], env=xmake_env(bdir))
    run([*COMPDB, bdir], env=xmake_env(bdir))
    # Lint is a gate: any finding in our own files fails `check`.
    tidy = ["clang-tidy", "--quiet", "-p", bdir, *(["--fix"] if args.fix else []), *units]
    try:
        print("+", " ".join(str(c) for c in tidy), flush=True)
        result = subprocess.run(resolve(tidy), check=False, cwd=ROOT, stdout=subprocess.PIPE, encoding="utf-8", errors="replace")
        print(result.stdout, end="", flush=True)
        failed, ignored = tidy_verdict(result.stdout, result.returncode)
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


def cmd_check(args):
    """The merge gate: commit messages, formatting and lint of the diff, then build and test every
    preset. The pre-commit hook checks formatting at each commit, so the hooks must be on; this
    format check covers what it misses (rebases and cherry-picks don't run it)."""
    if not hooks_enabled():
        raise Fail("enable the commit hooks, which check messages and formatting: git config core.hooksPath .githooks")
    cmd_selftest(args)
    rng = commit_range()
    if rng and cmd_commit_check(argparse.Namespace(range=rng, message_file=None)):
        raise Fail("commit-check failed")
    cmd_fmt(argparse.Namespace(check=True, staged=False, all=False, range=None))
    cmd_lint(argparse.Namespace(container=args.container, fix=False, all=False, range=None))
    skipped = [p for p in PRESETS if not buildable_here(p) or (args.container and not PRESETS[p]["image"])]
    for preset in [p for p in PRESETS if p not in skipped]:
        cmd_test(argparse.Namespace(preset=preset, mode="release", sanitizer="none", container=args.container,
                                    target=None, args=[], unity=False))
    # Unity builds are off (xmake.lua) but kept working: one test run with them on.
    unity_preset = NATIVE_PRESET if NATIVE_PRESET not in skipped else next(p for p in PRESETS if p not in skipped)
    cmd_test(argparse.Namespace(preset=unity_preset, mode="debug", sanitizer="none", container=args.container,
                                target=None, args=[], unity=True))
    if args.sanitizers:
        for preset, sanitizers in SANITIZERS.items():
            for sanitizer in sanitizers if preset not in skipped else ():
                cmd_test(argparse.Namespace(preset=preset, mode="debug", sanitizer=sanitizer, container=args.container,
                                            target=None, args=[], unity=False))
    print("check: all presets passed" + (" with every supported sanitizer" if args.sanitizers else "")
          + (f" (skipped {', '.join(skipped)}: not buildable on this host)" if skipped else ""))


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
    """dist/<APP>.app: the binary, conda's libc++ in Frameworks (the rpath in xmake.lua) and Info.plist."""
    contents = dist / f"{APP}.app" / "Contents"
    for d in ("MacOS", "Frameworks"):
        (contents / d).mkdir(parents=True)
    shutil.copy2(binary, contents / "MacOS")
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
    if not os.environ.get("GITHUB_REF", "").startswith("refs/tags/"):
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
    if "GITHUB_OUTPUT" in os.environ and not IN_CONTAINER:
        write_ci_artifact_name(args.preset)
    if not IN_CONTAINER and (PRESETS[args.preset]["image"] or needs_container(args.preset)):
        return reenter(args.preset, ["artifact", "--preset", args.preset])
    shutil.rmtree(ROOT / build_dir(args.preset, "release"), ignore_errors=True)  # clean room: nothing from earlier builds
    bdir = build(args.preset, "release")
    run(["xmake", "test", "--output-on-failure"], env=xmake_env(bdir))
    binary = binary_path(args.preset, "release")
    if PRESETS[args.preset]["plat"] == "linux":
        run(["patchelf", "--remove-rpath", binary])  # conda's GCC hard-codes an rpath into .pixi
        leaked = [line for line in capture(["ldd", binary]).splitlines() if ".pixi" in line]
        if leaked:
            raise Fail("artefact links into the Pixi environment:\n" + "\n".join(leaked))
    elif PRESETS[args.preset]["plat"] == "macosx":
        run(["install_name_tool", "-delete_rpath", Path(os.environ["CONDA_PREFIX"], "lib"), binary])
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
    cmd_test(argparse.Namespace(preset=preset, mode="coverage", sanitizer="none", container=False, target=None, args=[],
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
    run([os.environ.get("PIXI_EXE", "pixi"), "run", "-e", "profiler", "tracy-profiler"])


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
        print("See the git-commit skill or docs/design/0001 §11.", file=sys.stderr)
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
    report(hooks_enabled(), "commit hooks enabled (message and format checks)", "git config core.hooksPath .githooks")
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


def make_parser():
    parser = argparse.ArgumentParser(prog="project_n", description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)

    def add(name, func, help, preset=None, mode=False, container=False, choices=tuple(PRESETS)):
        p = sub.add_parser(name, help=help)
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

    p = add("doctor", cmd_doctor, "check the environment; prints how to fix problems, installs nothing")
    p.add_argument("--docs", action="store_true", help="only check that commands quoted in agent docs and README exist")
    add_sanitizer(add("build", cmd_build, "build a preset, or all of them", preset="native", mode=True, container=True,
                      choices=(*PRESETS, "all")))
    p = add("test", cmd_test, "build and run the tests of a preset", preset="native", mode=True, container=True)
    add_sanitizer(p)
    p.add_argument("--target", help="run one test target, forwarding the arguments after --")
    p.add_argument("--unity", action="store_true", help="with unity builds on (off by default; check runs this once)")
    p.add_argument("args", nargs=argparse.REMAINDER, help="-- <doctest arguments>, e.g. -- --test-case='*physics*'")
    p = add("run", cmd_run, "build and launch the game natively", preset="native", mode=True)
    p.add_argument("--debug", action="store_true", help="launch under the debugger (xmake run -d)")
    add_sanitizer(p)
    p = add("check", cmd_check, "the merge gate: commits, format and lint of the diff, build and test every preset", container=True)
    p.add_argument("--sanitizers", action="store_true", help="also test every preset under each supported sanitizer")
    add("artifact", cmd_artifact, "clean-room release build in the preset's container, into dist/", preset="required")
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
    add("profiler", cmd_profiler, "start the Tracy viewer; profile a `run --mode releasedbg` build")
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
    p.add_argument("--packages", action="store_true", help="also remove XMake package dirs of other environments")
    p.add_argument("--images", action="store_true", help="also remove this project's container images other than the current tags")
    return parser


def main(argv=None):
    args = make_parser().parse_args(argv)
    try:
        if HOST == "macos" and args.func not in (cmd_commit_check, cmd_selftest, cmd_fmt, cmd_release, cmd_docs, cmd_profiler):
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
