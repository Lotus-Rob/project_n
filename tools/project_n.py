"""project_n: the single build entry point. Run as `pixi run project_n <command>`.

Thin by design: every command reduces to XMake, Podman, git or git-cliff calls.
Standard library only. See tools/AGENTS.md and docs/design/0001.
"""

import argparse
import hashlib
import json
import os
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
CACHE_VOLUME = "project_n-xmake-cache"  # PROJECT_N_XMAKE_CACHE=<dir> bind-mounts a directory instead (CI caches it)

# Conventional Commits (docs/design/0001 §11.1). Add a scope in the commit that first uses it.
TYPES = ("feat", "fix", "perf", "refactor", "test", "build", "ci", "docs", "style", "chore", "revert")
SCOPES = ("repo", "env", "containers", "tools", "xmake", "deps", "tests", "design", "agents", "release")

HEADER_RE = re.compile(r"^(?P<type>[a-z]+)(?:\((?P<scope>[a-z0-9-]+)\))?(?P<breaking>!)?: (?P<desc>.+)$")
MAX_HEADER = 72
VERSION_RE = re.compile(r'set_version\("([^"]+)"\)')

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


def build_dir(preset, mode, sanitizer="none"):
    leaf = mode if sanitizer == "none" else f"{mode}-{sanitizer}"
    return Path("build", "container", preset, leaf) if IN_CONTAINER else Path("build", preset, leaf)


def packages_dir():
    """XMake keys its package cache on the toolchain name, not the compiler version, so each
    environment gets its own: a toolchain bump never reuses stale builds. pixi.toml counts too,
    because activation variables (build flags, pkg-config paths) aren't in the lock."""
    return Path.home() / ".xmake" / "project_n" / sha256("pixi.toml", "pixi.lock")[:12] / "packages"


def xmake_env(bdir):
    # Per-build config dir: native and container builds of one preset never share XMake state.
    return dict(os.environ, XMAKE_CONFIGDIR=str(ROOT / bdir / ".xmake"), XMAKE_PKG_INSTALLDIR=str(packages_dir()),
                **SANITIZER_ENV)


def check_sanitizer(preset, sanitizer):
    if sanitizer != "none" and sanitizer not in SANITIZERS[preset]:
        raise Fail(f"{preset} supports sanitizers: {', '.join(SANITIZERS[preset])}")


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
    cmd = ["podman", "build", "-t", tag, "-f", spec["file"]]
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


def configure(preset, mode, sanitizer="none"):
    check_sanitizer(preset, sanitizer)
    p, bdir = PRESETS[preset], build_dir(preset, mode, sanitizer)
    # -v makes XMake install packages one at a time (else 4 in parallel). shortcut: XMake installs
    # duplicate variants with the same build hash (libxcb, libx11 for SDL3) concurrently into one
    # directory and they corrupt each other; drop -v once that's fixed upstream.
    cmd = ["xmake", "f", "-y", "-v", "-p", p["plat"], "-a", p["arch"], f"--toolchain={p['toolchain']}", "-m", mode,
           "-o", bdir, f"--sanitizer={sanitizer}"]
    if p["toolchain"] == "msvc":
        cmd.append("--sdk=/opt/msvc" if IN_CONTAINER else f"--vs_toolset={VS_TOOLSET}")
    run(cmd, env=xmake_env(bdir))
    return bdir


def build(preset, mode, sanitizer="none"):
    bdir = configure(preset, mode, sanitizer)
    run(["xmake", "build"], env=xmake_env(bdir))
    if not IN_CONTAINER and sanitizer == "none":
        # Native builds keep clangd current (.clangd reads build/; container paths would be wrong).
        run(["xmake", "project", "-k", "compile_commands", ROOT / "build"], env=xmake_env(bdir))
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
        argv = ["test", "--preset", args.preset, "--mode", args.mode, "--sanitizer", args.sanitizer]
        argv += (["--target", args.target] if args.target else []) + (["--", *extra] if extra else [])
        return reenter(args.preset, argv)
    bdir = build(args.preset, args.mode, args.sanitizer)
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


def source_files():
    return sorted(str(p.relative_to(ROOT)) for d in ("src", "tests") for p in (ROOT / d).rglob("*") if p.suffix in (".cpp", ".hpp"))


# Findings are fixed properly first; a suppression is the last resort (root AGENTS.md). One that
# remains must be narrow and explained: NOLINT names its checks and gives a reason
# (`// NOLINT(check-name): why`), a diagnostic pragma carries a comment, a nested .clang-tidy
# says why in a `#` comment.
NOLINT_RE = re.compile(r"NOLINT(?P<kind>NEXTLINE|BEGIN|END)?(?:\((?P<checks>[^)]*)\))?(?P<rest>.*)")
PRAGMA_RE = re.compile(r"#\s*pragma\s+(?:(?:clang|GCC)\s+diagnostic\s+ignored|warning\s*\(\s*disable)")


def nolint_explained(line):
    m = NOLINT_RE.search(line)
    return not m or m["kind"] == "END" or bool(m["checks"] and re.match(r"\s*:\s*\S", m["rest"]))


def unexplained_suppressions():
    found = []
    for f in source_files():
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
                found.append(f"{p.relative_to(ROOT)}: explain in a # comment why these checks can't be met")
    return found


def cmd_fmt(args):
    run(["clang-format", *(["--dry-run", "-Werror"] if args.check else ["-i"]), *source_files()])


def cmd_lint(args):
    if needs_container(LINT_PRESET, args.container):
        return reenter(LINT_PRESET, ["lint", *(["--fix"] if args.fix else [])])
    found = unexplained_suppressions()
    if found:
        raise Fail("fix findings properly; a suppression is the last resort and must be narrow and explained:\n" + "\n".join(found))
    run(["ruff", "check", "tools"])
    run(["actionlint"])
    bdir = configure(LINT_PRESET, "debug")
    run(["xmake", "build"], env=xmake_env(bdir))
    run(["xmake", "project", "-k", "compile_commands", bdir], env=xmake_env(bdir))
    # Lint is a gate: .clang-tidy's WarningsAsErrors: '*' makes any finding fail `check`.
    tidy = ["clang-tidy", "--quiet", "-p", bdir, *(["--fix"] if args.fix else [])]
    try:
        run(tidy + [f for f in source_files() if f.endswith(".cpp")])
    finally:
        if args.fix:
            cmd_fmt(argparse.Namespace(check=False))  # clang-tidy's fixes don't follow .clang-format


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
    """The merge gate: commit messages, formatting, lint, then build and test every preset."""
    cmd_selftest(args)
    rng = commit_range()
    if rng and cmd_commit_check(argparse.Namespace(range=rng, message_file=None)):
        raise Fail("commit-check failed")
    if cmd_doctor(argparse.Namespace(docs=True)):
        raise Fail("agent docs quote commands that don't exist (agent-docs skill, contract rule 5)")
    cmd_fmt(argparse.Namespace(check=True))
    cmd_lint(argparse.Namespace(container=args.container, fix=False))
    skipped = [p for p in PRESETS if not buildable_here(p) or (args.container and not PRESETS[p]["image"])]
    for preset in [p for p in PRESETS if p not in skipped]:
        cmd_test(argparse.Namespace(preset=preset, mode="release", sanitizer="none", container=args.container,
                                    target=None, args=[]))
    if args.sanitizers:
        for preset, sanitizers in SANITIZERS.items():
            for sanitizer in sanitizers if preset not in skipped else ():
                cmd_test(argparse.Namespace(preset=preset, mode="debug", sanitizer=sanitizer, container=args.container,
                                            target=None, args=[]))
    print("check: all presets passed" + (" with every supported sanitizer" if args.sanitizers else "")
          + (f" (skipped {', '.join(skipped)}: not buildable on this host)" if skipped else ""))


def msvc_versions(env_sh):
    """The MSVC and SDK versions msvc-wine's wrappers use: the patch levels follow Microsoft's manifest."""
    found = dict(re.findall(r"^(MSVCVER|SDKVER)=(\S+)$", env_sh, re.MULTILINE))
    return {"msvc": found.get("MSVCVER"), "windows_sdk": found.get("SDKVER")}


def cmd_artifact(args):
    # Release artefacts come from committed sources only; the container sees the whole checkout.
    if capture(["git", "status", "--porcelain"]).strip():
        raise Fail("artifact builds committed sources only: commit or stash first (`check --container` tests uncommitted work)")
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
        run(["codesign", "--force", "--sign", "-", binary])  # arm64 refuses binaries whose ad-hoc signature broke
        shipped = ("/usr/lib/", "/System/", "@rpath/libc++.1.dylib")  # libc++ ships next to the binary
        leaked = [line for line in capture(["otool", "-L", binary]).splitlines()[1:] if not line.strip().startswith(shipped)]
        if leaked:
            raise Fail("artefact links libraries macOS doesn't ship:\n" + "\n".join(leaked))
    dist = ROOT / "dist" / args.preset
    shutil.rmtree(dist, ignore_errors=True)
    dist.mkdir(parents=True)
    shutil.copy2(ROOT / binary, dist)
    if PRESETS[args.preset]["plat"] == "macosx":
        shutil.copy2(Path(os.environ["CONDA_PREFIX"], "lib", "libc++.1.dylib"), dist)
    # Debug info XMake split off the release binary: kept for crash reports, never shipped to players.
    symbols = ROOT / binary.with_suffix({"linux": ".sym", "windows": ".pdb", "macosx": ".dSYM"}[PRESETS[args.preset]["plat"]])
    (dist / "symbols").mkdir()
    (shutil.copytree if symbols.is_dir() else shutil.copy2)(symbols, dist / "symbols" / symbols.name)
    manifest = {
        "version": read_version(),
        "commit": capture(["git", "rev-parse", "HEAD"]).strip(),
        "preset": args.preset,
        "image": os.environ.get("PROJECT_N_IMAGE", "unknown"),
        "pixi_lock_sha256": sha256("pixi.lock"),
        "files": {p.relative_to(dist).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in sorted(dist.rglob("*")) if p.is_file()},
    }
    # Toolchain parts pixi.lock doesn't pin.
    if PRESETS[args.preset]["toolchain"] == "msvc":
        manifest |= msvc_versions(Path("/opt/msvc/bin/x64/msvcenv.sh").read_text(encoding="utf-8"))
    elif PRESETS[args.preset]["plat"] == "macosx":
        manifest["macos_sdk"] = capture(["xcrun", "--show-sdk-version"]).strip()
    (dist / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"artifact: {dist.relative_to(ROOT)}")


def cmd_selftest(args):
    run([sys.executable, "-m", "unittest", "discover", "-q", "-s", "tools"])


def cmd_release(args):
    if capture(["git", "rev-parse", "--abbrev-ref", "HEAD"]).strip() != "main":
        raise Fail("release runs on main")
    if capture(["git", "status", "--porcelain"]).strip():
        raise Fail("working tree is not clean")
    run(["git", "fetch", "--quiet"])
    if capture(["git", "rev-list", "--count", "HEAD..@{upstream}"], check=False).strip() not in ("", "0"):
        raise Fail("main is behind its upstream: pull first")
    version = capture(["git", "cliff", "--bumped-version"]).strip().removeprefix("v")
    if args.dry_run:
        run(["git", "cliff", "--unreleased", "--tag", f"v{version}"])
        print(f"release --dry-run: would release v{version} (current {read_version()})")
        return
    xmake_lua = ROOT / "xmake.lua"
    xmake_lua.write_text(with_version(xmake_lua.read_text(encoding="utf-8"), version), encoding="utf-8")
    run(["git", "cliff", "--tag", f"v{version}", "-o", "CHANGELOG.md"])
    run(["git", "add", "xmake.lua", "CHANGELOG.md"])
    run(["git", "commit", "-m", f"chore(release): v{version}"])
    run(["git", "tag", "-a", f"v{version}", "-m", f"v{version}"])
    print(f"release: tagged v{version}; push with: git push --follow-tags")


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


def stale_doc_commands():
    """`project_n <cmd>` and `./dev <cmd>` quoted in AGENTS.md, CLAUDE.md, skills and README that don't exist."""
    commands = set(make_parser()._subparsers._group_actions[0].choices)
    docs = [*ROOT.glob("**/AGENTS.md"), *ROOT.glob("**/CLAUDE.md"), *ROOT.glob(".claude/skills/**/*.md"), ROOT / "README.md"]
    stale = []
    for doc in docs:
        if ".pixi" in doc.parts:
            continue
        for n, line in enumerate(doc.read_text(encoding="utf-8").splitlines(), 1):
            for cmd in re.findall(r"(?:project_n|\./dev) +([a-z][a-z-]*)", line):
                if cmd not in commands:
                    stale.append(f"{doc.relative_to(ROOT)}:{n}: unknown command {cmd!r}")
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
    pinned = re.search(r'set_xmakever\("([^"]+)"\)', (ROOT / "xmake.lua").read_text(encoding="utf-8"))[1]
    xmake = capture(["xmake", "--version"], check=False) if shutil.which("xmake") else ""
    report(pinned in xmake, f"xmake {pinned} from the Pixi env", "pixi install")
    hooks = capture(["git", "config", "core.hooksPath"], check=False).strip()
    report(hooks == ".githooks", "commit-msg hook enabled", "git config core.hooksPath .githooks")
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
        if podman and str(ROOT).lower().startswith(("c:", "d:")):
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
    version = re.search(r'set_xmakever\("([^"]+)"\)', (ROOT / "xmake.lua").read_text(encoding="utf-8"))[1]
    exe = Path.home() / ".xmake" / "project_n-tools" / f"xmake-{version}" / "bin" / "xmake"  # outside clean --packages' reach
    if not exe.exists():
        url = f"https://github.com/xmake-io/xmake/releases/download/v{version}/xmake-bundle-v{version}.macos.arm64"
        print(f"+ download {url}", flush=True)
        data = urllib.request.urlopen(url).read()
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

    p = add("doctor", cmd_doctor, "check the environment; prints how to fix problems, installs nothing")
    p.add_argument("--docs", action="store_true", help="only check that commands quoted in agent docs and README exist")
    add_sanitizer(add("build", cmd_build, "build a preset, or all of them", preset="native", mode=True, container=True,
                      choices=(*PRESETS, "all")))
    p = add("test", cmd_test, "build and run the tests of a preset", preset="native", mode=True, container=True)
    add_sanitizer(p)
    p.add_argument("--target", help="run one test target, forwarding the arguments after --")
    p.add_argument("args", nargs=argparse.REMAINDER, help="-- <doctest arguments>, e.g. -- --test-case='*physics*'")
    p = add("run", cmd_run, "build and launch the game natively", preset="native", mode=True)
    p.add_argument("--debug", action="store_true", help="launch under the debugger (xmake run -d)")
    add_sanitizer(p)
    p = add("check", cmd_check, "the merge gate: commits, fmt, lint, build and test every preset", container=True)
    p.add_argument("--sanitizers", action="store_true", help="also test every preset under each supported sanitizer")
    add("artifact", cmd_artifact, "clean-room release build in the preset's container, into dist/", preset="required")
    p = add("release", cmd_release, "bump the version with git-cliff, write CHANGELOG.md, commit and tag")
    p.add_argument("--dry-run", action="store_true")
    p = add("commit-check", cmd_commit_check, "validate commit messages (Conventional Commits)")
    group = p.add_mutually_exclusive_group(required=True)
    group.add_argument("message_file", nargs="?", help="a commit message file (used by the commit-msg hook)")
    group.add_argument("--range", help="validate every commit in a git range, e.g. main..HEAD")
    add("selftest", cmd_selftest, "run project_n.py's own unit tests (tools/test_project_n.py)")
    p = add("fmt", cmd_fmt, "format sources with the pinned clang-format")
    p.add_argument("--check", action="store_true", help="fail instead of rewriting files")
    p = add("lint", cmd_lint, "run the pinned clang-tidy over the sources (any finding fails)", container=True)
    p.add_argument("--fix", action="store_true", help="apply clang-tidy's fixes, then reformat")
    add("image", cmd_image, "build or pull container images if missing, and install MSVC for windows-msvc", preset="optional")
    add("shell", cmd_shell, "open a shell in a preset's container (to debug container failures)", preset="required")
    p = add("clean", cmd_clean, "remove build directories", preset="optional")
    p.add_argument("--packages", action="store_true", help="also remove XMake package dirs of other environments")
    p.add_argument("--images", action="store_true", help="also remove this project's container images other than the current tags")
    return parser


def main(argv=None):
    args = make_parser().parse_args(argv)
    try:
        if HOST == "macos":
            ensure_macos_xmake()
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
