"""project_n: the single build entry point. Run as `pixi run project_n <command>`.

Thin by design: every command reduces to XMake, Podman, git or git-cliff calls.
Standard library only. See tools/AGENTS.md and docs/design/0001.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Conventional Commits (docs/design/0001 §11.1). Add a scope in the commit that first uses it.
TYPES = ("feat", "fix", "perf", "refactor", "test", "build", "ci", "docs", "style", "chore", "revert")
SCOPES = ("repo", "env", "containers", "tools", "xmake", "deps", "tests", "design", "agents", "release")

HEADER_RE = re.compile(r"^(?P<type>[a-z]+)(?:\((?P<scope>[a-z0-9-]+)\))?(?P<breaking>!)?: (?P<desc>.+)$")
MAX_HEADER = 72


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


def run(cmd, **kwargs):
    """Run a command from the repo root, echoing it; raise on failure."""
    print("+", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.run(cmd, cwd=ROOT, check=True, **kwargs)


def capture(cmd):
    return subprocess.run(cmd, cwd=ROOT, check=True, capture_output=True, text=True).stdout


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


def main(argv=None):
    parser = argparse.ArgumentParser(prog="project_n", description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("commit-check", help="validate commit messages (Conventional Commits)")
    group = p.add_mutually_exclusive_group(required=True)
    group.add_argument("message_file", nargs="?", help="a commit message file (used by the commit-msg hook)")
    group.add_argument("--range", help="validate every commit in a git range, e.g. main..HEAD")
    p.set_defaults(func=cmd_commit_check)

    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except subprocess.CalledProcessError as e:
        if e.stderr:
            print(e.stderr, end="", file=sys.stderr)
        return e.returncode or 1


if __name__ == "__main__":
    sys.exit(main())
