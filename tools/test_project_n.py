"""Self-checks for project_n.py. Run: pixi run python -m unittest discover -s tools"""

import argparse
import contextlib
import fnmatch
import functools
import io
import json
import os
import re
import subprocess
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

import project_n


class CommitCheckTest(unittest.TestCase):
    def ok(self, message, **kwargs):
        self.assertEqual(project_n.check_message(message, **kwargs), [], message)

    def bad(self, message, fragment, **kwargs):
        problems = project_n.check_message(message, **kwargs)
        self.assertTrue(any(fragment in p for p in problems), f"{message!r} -> {problems}")

    def test_valid_headers(self):
        self.ok("docs(design): add 0001 repository and build environment")
        self.ok("feat: add title screen")
        self.ok("build(tools)!: rename dist to artifact\n\nBREAKING CHANGE: use artifact.")
        self.ok("fix(env): pin clang\n\nBody line.\n# git comment ignored")

    def test_rejects_bad_type_and_scope(self):
        self.bad("feature: add thing", "unknown type")
        self.bad("feat(nowhere): add thing", "unknown scope")
        self.bad("add thing", "header must be")

    def test_description_rules(self):
        self.bad("feat: Add thing", "lowercase")
        self.bad("feat: add thing.", "period")
        self.bad("feat: " + "x" * 80, "limit is 72")

    def test_body_needs_blank_line(self):
        self.bad("feat: add thing\nbody right away", "blank line")

    def test_fixups_only_allowed_locally(self):
        self.ok("fixup! feat: add thing")
        self.bad("fixup! feat: add thing", "squashed", allow_fixup=False)

    def test_rejects_merge_and_empty(self):
        self.bad("Merge branch 'x'", "merge commits")
        self.bad("\n# only comments\n", "empty")


class LocationRuleTest(unittest.TestCase):
    def setUp(self):
        self.saved = (project_n.HOST, project_n.IN_CONTAINER)

    def tearDown(self):
        project_n.HOST, project_n.IN_CONTAINER = self.saved

    def test_native_when_host_matches(self):
        project_n.HOST, project_n.IN_CONTAINER = "linux", False
        self.assertFalse(project_n.needs_container("linux-clang"))
        self.assertTrue(project_n.needs_container("windows-msvc"))
        self.assertTrue(project_n.needs_container("linux-gcc", force=True))

    def test_windows_host(self):
        project_n.HOST, project_n.IN_CONTAINER = "windows", False
        self.assertFalse(project_n.needs_container("windows-msvc"))
        self.assertTrue(project_n.needs_container("linux-gcc"))

    def test_macos_builds_only_on_a_mac(self):
        project_n.HOST, project_n.IN_CONTAINER = "linux", False
        self.assertFalse(project_n.buildable_here("macos-clang"))
        self.assertTrue(project_n.buildable_here("windows-msvc"))
        with self.assertRaises(project_n.Fail):
            project_n.reenter("macos-clang", ["build"])
        project_n.HOST = "macos"
        self.assertTrue(project_n.buildable_here("macos-clang"))
        project_n.HOST, project_n.IN_CONTAINER = "linux", True
        self.assertFalse(project_n.buildable_here("macos-clang"))

    def test_never_reenters_inside_a_container(self):
        project_n.HOST, project_n.IN_CONTAINER = "linux", True
        self.assertFalse(project_n.needs_container("windows-msvc", force=True))
        self.assertEqual(project_n.build_dir("linux-gcc", "release").parts, ("build", "container", "linux-gcc", "release"))


class NativePresetTest(unittest.TestCase):
    def test_native_preset_builds_without_a_container(self):
        self.assertFalse(project_n.needs_container(project_n.NATIVE_PRESET))


class BuildAllTest(unittest.TestCase):
    def test_builds_every_preset_past_failures(self):
        built, saved = [], project_n.cmd_build

        def fake_build(args):
            if args.preset == "all":
                return saved(args)
            built.append(args.preset)
            if args.preset == "linux-gcc":
                raise project_n.subprocess.CalledProcessError(1, "xmake")

        project_n.cmd_build = fake_build
        try:
            with contextlib.redirect_stdout(io.StringIO()):  # its summary reports the faked failure
                result = project_n.build_all(project_n.argparse.Namespace(preset="all", mode="debug", sanitizer="none", container=False))
        finally:
            project_n.cmd_build = saved
        self.assertEqual(built, [p for p in project_n.PRESETS if project_n.buildable_here(p)])
        self.assertEqual(result, 1)


def git(root, *args):
    subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false", *args],
                   cwd=root, check=True, capture_output=True)


@contextlib.contextmanager
def temp_repo(files):
    """A throwaway git repository on main with `files` committed, standing in for project_n.ROOT. The
    host's PROJECT_N_COMMIT_RANGE (CI sets one) names commits this repository doesn't have."""
    saved = project_n.ROOT
    with tempfile.TemporaryDirectory() as d, mock.patch.dict(project_n.os.environ):
        project_n.os.environ.pop("PROJECT_N_COMMIT_RANGE", None)
        root = project_n.ROOT = Path(d)
        try:
            git(root, "init", "-q", "-b", "main")
            for name, text in files.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_text(text)
            git(root, "add", ".")
            git(root, "commit", "-q", "--no-verify", "-m", "init")
            yield root
        finally:
            project_n.ROOT = saved


class DirtyTreeTest(unittest.TestCase):
    def test_lock_reorders_dont_block_releases(self):
        with temp_repo({"xmake-requires.lock": "a", "xmake.lua": "a"}) as root:
            (root / "xmake-requires.lock").write_text("b")
            self.assertFalse(project_n.tree_dirty())
            (root / "xmake.lua").write_text("b")
            self.assertTrue(project_n.tree_dirty())


class RestoreLockTest(unittest.TestCase):
    # A small lock as XMake writes it: two platforms, entries with nested fields, CRLF on one line.
    META = '    __meta__ = {\n        version = "1.0"\n    }'
    SDL = '        ["sdl#1"] = {\n            version = "3.4"\n        }'
    TTF = '        ["ttf#1"] = {\n            repo = {\n                commit = "aa"\n            },\n            version = "3.2"\n        }'
    MESON = '        ["meson#1"] = {\n            version = "1.12"\n        }'
    ZLIB = '        ["zlib#1"] = {\n            version = "1.3"\n        }'

    @staticmethod
    def lock(*blocks, windows=(), meta_last=False):
        sections = [f'    ["linux|x86_64"] = {{\n{",\n".join(blocks)}\n    }}']
        if windows:
            sections.append(f'    ["windows|x64"] = {{\n{",\n".join(windows)}\n    }}')
        parts = [*sections, RestoreLockTest.META] if meta_last else [RestoreLockTest.META, *sections]
        return ("{\r\n" + ",\n".join(parts) + "\n}\n").encode()

    @property
    def BEFORE(self):
        return self.lock(self.SDL, self.TTF, windows=(self.MESON, self.SDL))

    def restored(self, now):
        """The lock after restore_lock_unless_added, and what it printed (captured: selftest's output stays clean)."""
        out = io.StringIO()
        with temp_repo({"xmake-requires.lock": "x"}) as root, contextlib.redirect_stdout(out):
            lock = root / "xmake-requires.lock"
            lock.write_bytes(now)
            project_n.restore_lock_unless_added(self.BEFORE)
            return lock.read_bytes(), out.getvalue()

    def changed_lines(self, printed):
        lines = printed.splitlines()
        self.assertIn("xmake-requires.lock", lines[0])
        self.assertIn("commit", lines[0])
        return [line for line in lines if line.startswith(("+", "-")) and line[:3] not in ("+++", "---")]

    def test_reordered_lock_is_restored_byte_for_byte_silently(self):
        reordered = self.lock(self.TTF, self.SDL, windows=(self.SDL, self.MESON), meta_last=True)
        self.assertEqual(self.restored(reordered), (self.BEFORE, ""))

    def test_unchanged_lock_prints_nothing(self):
        self.assertEqual(self.restored(self.BEFORE), (self.BEFORE, ""))

    def test_pruned_entries_are_restored_silently(self):
        # The windows-msvc container drops native Windows' build tools from windows|x64, and its own run
        # reorders: neither is a change to commit.
        for now in (self.lock(self.SDL, self.TTF, windows=(self.SDL,)),
                    self.lock(self.TTF, self.SDL, meta_last=True)):
            self.assertEqual(self.restored(now), (self.BEFORE, ""), now)

    def test_added_or_changed_entry_is_kept_and_its_diff_printed_to_commit(self):
        # CI's macos job added its missing entries every run, unseen: the log must show them.
        changed = self.lock(self.SDL, self.TTF.replace('"aa"', '"bb"'), windows=(self.MESON, self.SDL))
        added = self.lock(self.SDL, self.TTF, self.ZLIB, windows=(self.MESON, self.SDL))
        for now, diff in ((changed, ['-                commit = "aa"', '+                commit = "bb"']),
                          (added, ["+        },", '+        ["zlib#1"] = {', '+            version = "1.3"'])):
            lock, printed = self.restored(now)
            self.assertEqual(lock, now)
            self.assertEqual(self.changed_lines(printed), diff)

    def test_removal_with_an_addition_is_kept_and_printed(self):
        now = self.lock(self.SDL, self.TTF, windows=(self.ZLIB, self.SDL))
        lock, printed = self.restored(now)
        self.assertEqual(lock, now)
        self.assertIn('+        ["zlib#1"] = {', self.changed_lines(printed))

    def test_unreadable_lock_is_kept_and_printed(self):
        now = self.BEFORE.replace(b'version = "3.4"', b'version = ')
        lock, printed = self.restored(now)
        self.assertEqual(lock, now)
        self.assertIn('+            version = ', self.changed_lines(printed))

    def test_unchanged_lock_is_not_written(self):
        with temp_repo({"xmake-requires.lock": "x"}) as root:
            lock = root / "xmake-requires.lock"
            lock.write_bytes(self.BEFORE)
            os.utime(lock, ns=(1_000_000_000, 1_000_000_000))
            project_n.restore_lock_unless_added(self.BEFORE)
            self.assertEqual(lock.stat().st_mtime_ns, 1_000_000_000)

    @property
    def REORDERED(self):
        """What the fake XMake below writes: BEFORE with its linux entries swapped."""
        return self.lock(self.TTF, self.SDL, windows=(self.MESON, self.SDL))

    def reordering_xmake(self, root, fail):
        """A subprocess.run that reorders the lock as XMake does, and fails when asked."""
        def fake(cmd, **kwargs):
            (root / "xmake-requires.lock").write_bytes(self.REORDERED)
            if fail:
                raise project_n.subprocess.CalledProcessError(1, cmd)
        return mock.patch.object(project_n.subprocess, "run", fake)

    def test_every_xmake_call_restores_the_lock_even_when_it_fails(self):
        for fail in (False, True):
            with temp_repo({"xmake-requires.lock": "x"}) as root:
                (root / "xmake-requires.lock").write_bytes(self.BEFORE)
                with self.reordering_xmake(root, fail):
                    if fail:
                        with self.assertRaises(project_n.subprocess.CalledProcessError):
                            project_n.run(["xmake", "build"])
                    else:
                        project_n.run(["xmake", "test"])
                self.assertEqual((root / "xmake-requires.lock").read_bytes(), self.BEFORE)

    def test_configure_restores_the_lock(self):
        with temp_repo({"xmake-requires.lock": "x"}) as root:
            (root / "xmake-requires.lock").write_bytes(self.BEFORE)
            with self.reordering_xmake(root, False), mock.patch.object(project_n, "xmake_env", lambda bdir: {}):
                project_n.configure("linux-clang", "debug")
            self.assertEqual((root / "xmake-requires.lock").read_bytes(), self.BEFORE)

    def test_other_commands_leave_the_lock_alone(self):
        with temp_repo({"xmake-requires.lock": "x"}) as root:
            (root / "xmake-requires.lock").write_bytes(self.BEFORE)
            with self.reordering_xmake(root, False):
                project_n.run(["git", "status"])
            self.assertEqual((root / "xmake-requires.lock").read_bytes(), self.REORDERED)


class ScopeTest(unittest.TestCase):
    DIFF = project_n.argparse.Namespace(all=False, range=None)

    def test_diff_covers_changed_and_new_sources_only(self):
        with temp_repo({"src/a.cpp": "a", "src/b.cpp": "b"}) as root:
            (root / "src" / "a.cpp").write_text("changed")
            (root / "src" / "c.cpp").write_text("new")
            self.assertEqual(project_n.scoped_sources(self.DIFF)[0], ["src/a.cpp", "src/c.cpp"])

    def test_a_branch_covers_its_committed_changes(self):
        with temp_repo({"src/a.cpp": "a", "src/b.cpp": "b"}) as root:
            git(root, "switch", "-q", "-c", "topic")
            (root / "src" / "a.cpp").write_text("committed on the branch")
            git(root, "commit", "-q", "--no-verify", "-am", "change a")
            self.assertEqual(project_n.scoped_sources(self.DIFF)[0], ["src/a.cpp"])

    def test_source_paths_use_gits_forward_slashes(self):
        with temp_repo({"src/core/a.cpp": "a"}):
            self.assertEqual(project_n.source_files(), ["src/core/a.cpp"])

    def test_a_clone_without_local_main_diffs_against_origin_main(self):
        with temp_repo({"src/a.cpp": "a"}) as root:
            git(root, "update-ref", "refs/remotes/origin/main", "HEAD")
            git(root, "switch", "-q", "-c", "topic")
            git(root, "branch", "-q", "-D", "main")
            self.assertEqual(project_n.scope_range(), "origin/main..HEAD")

    def test_main_diffs_against_its_commit_range(self):
        with temp_repo({"src/a.cpp": "a", "src/b.cpp": "b"}) as root:
            base = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root, capture_output=True, text=True, check=True).stdout.strip()
            (root / "src" / "a.cpp").write_text("pushed")
            git(root, "commit", "-q", "--no-verify", "-am", "change a")
            project_n.os.environ["PROJECT_N_COMMIT_RANGE"] = f"{base}..HEAD"  # what CI sets on main
            self.assertEqual(project_n.scoped_sources(self.DIFF)[0], ["src/a.cpp"])

    def test_container_lint_gets_the_range(self):
        argvs = []
        with mock.patch.object(project_n, "reenter", lambda preset, argv: argvs.append(argv)), \
             mock.patch.object(project_n, "IN_CONTAINER", False):
            project_n.cmd_lint(project_n.argparse.Namespace(container=True, fix=False, all=False, range="a..b"))
        self.assertEqual(argvs[0][-2:], ["--range", "a..b"])

    def test_container_lint_gets_all(self):
        argvs = []
        with mock.patch.object(project_n, "reenter", lambda preset, argv: argvs.append(argv)), \
             mock.patch.object(project_n, "IN_CONTAINER", False):
            project_n.cmd_lint(project_n.argparse.Namespace(container=True, fix=False, all=True, range="a..b"))
        self.assertIn("--all", argvs[0])

    def test_all_covers_every_source(self):
        with temp_repo({"src/a.cpp": "a", "src/b.cpp": "b"}):
            everything = project_n.argparse.Namespace(all=True, range=None)
            self.assertEqual(project_n.scoped_sources(everything), (["src/a.cpp", "src/b.cpp"], None))

    def test_a_widening_change_covers_every_source(self):
        with temp_repo({"src/a.cpp": "a", "src/b.cpp": "b", ".clang-tidy": "x"}) as root:
            (root / ".clang-tidy").write_text("y")
            self.assertEqual(project_n.scoped_sources(self.DIFF), (["src/a.cpp", "src/b.cpp"], None))

    def test_deleting_a_nested_lint_config_covers_every_source(self):
        with temp_repo({"src/a.cpp": "a", "src/core/b.cpp": "b", "src/core/.clang-tidy": "# x"}) as root:
            (root / "src" / "core" / ".clang-tidy").unlink()
            self.assertEqual(project_n.scoped_sources(self.DIFF), (["src/a.cpp", "src/core/b.cpp"], None))

    def test_lint_config_build_and_rules_widen_the_scope(self):
        for path in (".clang-tidy", "src/core/.clang-tidy", ".clang-format", ".clangd", "xmake.lua", "tests/xmake.lua",
                     "xmake/repo/packages/t/tracy-libdl/xmake.lua", "pixi.lock", "tools/project_n.py", "docs/Doxyfile"):
            self.assertTrue(project_n.widens_scope(path), path)
        self.assertFalse(project_n.widens_scope("src/main.cpp"))

    def test_a_base_missing_from_the_clone_covers_everything(self):
        with temp_repo({"src/a.cpp": "a"}):
            self.assertIsNone(project_n.changed_files("0123456789abcdef0123456789abcdef01234567..HEAD"))



UNITS_FILES = {"src/a.cpp": "a", "src/b.cpp": "b", "src/a.hpp": "h"}
UNITS_DEPS = {"src/a.cpp": frozenset({"src/a.cpp", "src/a.hpp"}), "src/b.cpp": frozenset({"src/b.cpp"})}


class LintUnitsTest(unittest.TestCase):

    def test_a_changed_header_lints_the_units_that_reach_it(self):
        with temp_repo(UNITS_FILES):
            self.assertEqual(project_n.lint_units(["src/a.hpp"], UNITS_DEPS), ["src/a.cpp"])

    def test_a_header_every_unit_reaches_lints_every_unit(self):
        deps = {unit: names | {"src/pch.hpp"} for unit, names in UNITS_DEPS.items()}
        with temp_repo({**UNITS_FILES, "src/pch.hpp": "p"}):
            self.assertEqual(project_n.lint_units(["src/pch.hpp"], deps), ["src/a.cpp", "src/b.cpp"])

    def test_a_unit_without_a_record_is_always_linted(self):
        with temp_repo(UNITS_FILES):
            self.assertIn("src/b.cpp", project_n.lint_units(["src/a.hpp"], {"src/a.cpp": UNITS_DEPS["src/a.cpp"]}))

    def test_a_record_for_a_deleted_unit_is_ignored(self):
        deps = {**UNITS_DEPS, "src/gone.cpp": frozenset({"src/gone.cpp", "src/a.hpp"})}
        with temp_repo(UNITS_FILES):
            self.assertEqual(project_n.lint_units(["src/a.hpp"], deps), ["src/a.cpp"])

    def test_a_changed_source_file_lints_only_itself(self):
        with temp_repo(UNITS_FILES):
            self.assertEqual(project_n.lint_units(["src/a.cpp"], UNITS_DEPS), ["src/a.cpp"])

    def test_every_source_lints_every_unit(self):
        with temp_repo(UNITS_FILES):
            self.assertEqual(project_n.lint_units(project_n.source_files(), {}), ["src/a.cpp", "src/b.cpp"])

    def test_a_header_no_record_names_is_reported(self):
        self.assertEqual(project_n.unreached_headers(["src/new.hpp", "src/a.hpp", "src/a.cpp"], UNITS_DEPS), ["src/new.hpp"])

    @staticmethod
    def build_records(root, records, compiled):
        """A lint build in root/bdir: XMake's dependency records (unit -> its Lua `depfiles` string, escapes included)
        and a compile database listing `compiled`."""
        for unit, depfiles in records.items():
            record = root / "bdir/.deps/core/linux/x86_64/debug" / f"{unit}.o.d"
            record.parent.mkdir(parents=True, exist_ok=True)
            record.write_text(f'{{\n    depfiles = "{depfiles}",\n    values = {{ }}\n}}\n')
        (root / "bdir/compile_commands.json").write_text(
            json.dumps([{"directory": str(root), "arguments": ["clang++", unit], "file": unit} for unit in compiled]))
        return root / "bdir"

    def test_records_spell_paths_relative_and_absolute(self):
        with temp_repo(UNITS_FILES) as root:
            lua_root = str(root).replace("\\", "\\\\")  # as XMake writes a Windows path into a Lua string
            bdir = self.build_records(root, {"src/a.cpp": "bdir/.objs/core/linux/x86_64/debug/src/__cpp_a.cpp.cpp:   "
                                             f"src/a.cpp   {lua_root}/src/b.hpp   /opt/pkg/include/x.h\\\n   src/c.hpp\\\n"},
                                      ["src/a.cpp"])
            self.assertEqual(project_n.unit_dependencies(bdir),
                             {"src/a.cpp": frozenset({"src/a.cpp", "src/b.hpp", "src/c.hpp"})})

    def test_records_escape_spaces_in_paths(self):
        with temp_repo({"my src/a.cpp": "a"}) as root:
            bdir = self.build_records(root, {"a.cpp": "bdir/__cpp_a.cpp.cpp: my\\\\ src/a.cpp my\\\\ src/a.hpp"},
                                      ["my src/a.cpp"])
            self.assertEqual(project_n.unit_dependencies(bdir), {"my src/a.cpp": frozenset({"my src/a.cpp", "my src/a.hpp"})})

    def test_a_record_lints_build_no_longer_compiles_is_ignored(self):
        with temp_repo(UNITS_FILES) as root:
            bdir = self.build_records(root, {"src/a.cpp": "x.o: src/a.cpp", "src/b.cpp": "y.o: src/b.cpp src/a.hpp"},
                                      ["src/a.cpp"])
            self.assertEqual(project_n.unit_dependencies(bdir), {"src/a.cpp": frozenset({"src/a.cpp"})})


class StagedFormatTest(unittest.TestCase):
    CLANG_FORMAT = (project_n.ROOT / ".clang-format").read_text()

    def check(self):
        with contextlib.redirect_stdout(io.StringIO()):  # clang-format's report of the expected failure
            project_n.check_staged_format()

    def test_a_badly_formatted_staged_file_fails(self):
        with temp_repo({".clang-format": self.CLANG_FORMAT, "src/a.cpp": "int x = 0;\n"}) as root:
            (root / "src" / "a.cpp").write_text("int   x=0;\n")
            git(root, "add", "src/a.cpp")
            with self.assertRaises(project_n.Fail):
                self.check()

    def test_a_formatted_staged_file_passes(self):
        with temp_repo({".clang-format": self.CLANG_FORMAT, "src/a.cpp": "int x = 0;\n"}) as root:
            (root / "src" / "a.cpp").write_text("int y = 0;\n")
            git(root, "add", "src/a.cpp")
            self.check()

    def test_a_staged_nested_clang_format_checks_every_source(self):
        with temp_repo({".clang-format": self.CLANG_FORMAT, "src/core/a.cpp": "int   x=0;\n"}) as root:
            (root / "src" / "core" / ".clang-format").write_text("BasedOnStyle: InheritParentConfig\n")
            git(root, "add", "src/core/.clang-format")
            with self.assertRaises(project_n.Fail):  # a.cpp isn't staged, but its format rules changed
                self.check()

    def test_a_staged_file_deleted_from_disk_is_still_checked(self):
        with temp_repo({".clang-format": self.CLANG_FORMAT, "src/a.cpp": "int x = 0;\n"}) as root:
            (root / "src" / "new.cpp").write_text("int   y=0;\n")
            git(root, "add", "src/new.cpp")
            (root / "src" / "new.cpp").unlink()  # only the index has it now, and it would be committed
            with self.assertRaises(project_n.Fail):
                self.check()

    def test_the_staged_content_is_checked_not_the_working_tree(self):
        with temp_repo({".clang-format": self.CLANG_FORMAT, "src/a.cpp": "int x = 0;\n"}) as root:
            (root / "src" / "a.cpp").write_text("int   x=0;\n")
            git(root, "add", "src/a.cpp")
            (root / "src" / "a.cpp").write_text("int x = 0;\n")  # fixed, but not staged
            with self.assertRaises(project_n.Fail):
                self.check()


class CheckTest(unittest.TestCase):
    def test_check_refuses_a_clone_not_set_up(self):
        ran = []
        with mock.patch.object(project_n, "clone_set_up", lambda: False), \
             mock.patch.object(project_n, "cmd_selftest", lambda args: ran.append("selftest")), \
             self.assertRaises(project_n.Fail):
            project_n.cmd_check(project_n.argparse.Namespace(container=False, sanitizers=False))
        self.assertEqual(ran, [])  # refused before running anything

    def container_check(self, out=None, **patches):
        tests, builds, references, out = [], [], [], out or io.StringIO()
        steps = {"clone_set_up": lambda: True, "cmd_selftest": lambda args: None, "commit_range": lambda: None,
                 "cmd_fmt": lambda args: None, "cmd_lint": lambda args: None, "cmd_reference": references.append,
                 "cmd_test": tests.append, "cmd_build": builds.append}
        with mock.patch.multiple(project_n, **{**steps, **patches}), contextlib.redirect_stdout(out):
            project_n.cmd_check(project_n.argparse.Namespace(container=True, sanitizers=False))
        return tests, builds, references, out.getvalue()

    def test_check_prints_each_steps_time(self):
        out = self.container_check()[3]
        presets = [p for p in project_n.PRESETS if project_n.PRESETS[p]["image"]]
        expected = ["selftest", "fmt", "lint", *(f"test {p} release" for p in presets), "reference", "unity test",
                    "releasedbg", "total"]
        positions = [out.find(name) for name in expected]
        self.assertNotIn(-1, positions, out)
        self.assertEqual(positions, sorted(positions), out)

    def test_step_times_print_when_a_step_fails(self):
        def fail(args):
            raise project_n.Fail("boom")
        out = io.StringIO()
        with self.assertRaises(project_n.Fail):
            self.container_check(out, cmd_test=fail)
        self.assertIn("lint", out.getvalue())
        self.assertRegex(out.getvalue(), r"test \S+ release")

    def test_check_compares_the_reference_pictures_once_on_linux(self):
        references = self.container_check()[2]
        self.assertEqual(len(references), 1)
        self.assertEqual(project_n.PRESETS[references[0].preset]["plat"], "linux")

    def test_container_check_runs_unity_once_on_a_preset_with_an_image(self):
        unity = [args for args in self.container_check()[0] if args.unity]
        self.assertEqual(len(unity), 1)
        self.assertTrue(project_n.PRESETS[unity[0].preset]["image"], unity[0].preset)

    def test_check_builds_releasedbg_once(self):
        builds = self.container_check()[1]
        self.assertEqual([args.mode for args in builds], ["releasedbg"])
        self.assertTrue(project_n.PRESETS[builds[0].preset]["image"], builds[0].preset)


class RunReferenceCaseTest(unittest.TestCase):
    def run_cases(self, returncode, indices=(1, 2)):
        """Runs `run_reference_case` for each index with a fake subprocess; returns the XDG_RUNTIME_DIR each saw."""
        seen = []

        def fake_run(argv, **kwargs):
            directory = Path(kwargs["env"]["XDG_RUNTIME_DIR"])
            self.assertTrue(directory.is_dir())
            if os.name != "nt":
                self.assertEqual(directory.stat().st_mode & 0o777, 0o700)
            self.assertEqual(kwargs["env"]["VK_DRIVER_FILES"], "x")
            seen.append(directory)
            return subprocess.CompletedProcess([], returncode, "")
        with mock.patch.object(project_n.subprocess, "run", fake_run):
            for index in indices:
                project_n.run_reference_case("exe", {"XDG_RUNTIME_DIR": "/shared", "VK_DRIVER_FILES": "x"}, index)
        return seen

    def test_each_case_gets_its_own_runtime_directory(self):
        first, second = self.run_cases(0)
        self.assertNotEqual(first, second)
        for directory in (first, second):
            self.assertNotEqual(directory, Path("/shared"))
            self.assertFalse(directory.exists())

    def test_the_runtime_directory_goes_when_a_case_crashes(self):
        (directory,) = self.run_cases(-11, indices=(1,))
        self.assertFalse(directory.exists())


class ReferenceTest(unittest.TestCase):
    def references(self, update, committed=(), case=None):
        """Runs `run_references` in a temporary root holding the `committed` pictures, over three cases run by `case`
        (index -> (output, return code)); by default case 2 draws a.png and fails. Returns (a.png's bytes or None, the
        indices run, stdout)."""
        ran, out = [], io.StringIO()

        def draw_and_fail(index):
            if index == 2:
                (root / "build" / "reference" / "a.png").write_bytes(b"drawn")
                return "case 2 failed\n", 1
            return "", 0

        def run_case(exe, env, index):
            ran.append(index)
            return (case or draw_and_fail)(index)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "tests" / "reference").mkdir(parents=True)
            for name in committed:
                (root / "tests" / "reference" / name).write_bytes(b"committed")
            try:
                with mock.patch.multiple(project_n, ROOT=root, capture=lambda argv: '{"environments_info": [{"name": "lavapipe", "prefix": "/lavapipe"}]}',
                                         run=lambda argv, env=None: None, reference_case_count=lambda exe, env: 3,
                                         run_reference_case=run_case, binary_path=lambda *args: Path("reference_tests")), \
                     contextlib.redirect_stdout(out):
                    project_n.run_references("linux-gcc", update)
            finally:
                self.ran, self.out = sorted(ran), out.getvalue()
            picture = root / "tests" / "reference" / "a.png"
            return picture.read_bytes() if picture.exists() else None

    def test_a_failing_comparison_fails(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.references(update=False)

    def test_update_takes_the_pictures_of_a_failing_run(self):
        self.assertEqual(self.references(update=True), b"drawn")

    def test_update_refuses_a_run_that_didnt_draw_every_view(self):
        with self.assertRaises(project_n.Fail):
            self.references(update=True, committed=["a.png", "b.png"])

    def test_every_case_runs_once(self):
        self.references(update=False, case=lambda index: ("", 0))
        self.assertEqual(self.ran, [1, 2, 3])

    def test_every_case_runs_when_one_fails(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.references(update=False, case=lambda index: ("", 1 if index == 1 else 0))
        self.assertEqual(self.ran, [1, 2, 3])

    def test_a_failure_ends_naming_the_failed_cases(self):
        outputs = {1: ("TEST CASE:  the map cells at zoom 1\nERROR: CHECK\n", 1), 2: ("", 0), 3: ("", -11)}
        with self.assertRaises(subprocess.CalledProcessError) as raised:
            self.references(update=False, case=outputs.__getitem__)
        self.assertIn("2 of 3", raised.exception.stderr)
        self.assertIn("the map cells at zoom 1", raised.exception.stderr)
        self.assertIn("case 3", raised.exception.stderr)  # crashed before doctest named it

    def test_outputs_print_in_case_order(self):
        def finish_out_of_order(index):
            time.sleep(0.01 * (3 - index))  # case 3 finishes first
            return ["one", "two", "three"][index - 1], 0
        self.references(update=False, case=finish_out_of_order)
        self.assertLess(self.out.index("one"), self.out.index("two"))
        self.assertLess(self.out.index("two"), self.out.index("three"))

    def test_the_case_count_is_read_from_doctest(self):
        line = "[doctest] unskipped test cases passing the current filters: 25\n"
        with mock.patch.object(project_n.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, line)):
            self.assertEqual(project_n.reference_case_count(Path("reference_tests"), {}), 25)
        with mock.patch.object(project_n.subprocess, "run", return_value=subprocess.CompletedProcess([], 1, "crashed\n")), \
             self.assertRaises(project_n.Fail):
            project_n.reference_case_count(Path("reference_tests"), {})

    def test_reference_refuses_a_preset_that_isnt_linux(self):
        with self.assertRaises(project_n.Fail):
            project_n.cmd_reference(project_n.argparse.Namespace(preset="windows-msvc", container=False, update=False))


class MsvcWrapperTest(unittest.TestCase):
    ARGS = ("-c", "/sourceDependencies", "dep.json", "a b.cpp", "-Fosrc/sourceDependencies.obj", "-sourceDependencies",
            "x.json", "-nologo")

    def wrapped(self, env):
        """The arguments the wrapper hands msvc-wine's cl, run with ARGS in `env`."""
        with tempfile.TemporaryDirectory() as tmp:
            for name, text in (("cl.exe", project_n.CL_WRAPPER), ("cl", "#!/usr/bin/env bash\nprintf '%s\\n' \"$@\"\n")):
                (Path(tmp) / name).write_text(text)
                (Path(tmp) / name).chmod(0o755)
            return subprocess.run([Path(tmp) / "cl.exe", *self.ARGS], check=True, capture_output=True, text=True,
                                  env=env).stdout.splitlines()

    @unittest.skipIf(os.name == "nt", "bash script")
    def test_the_wrapper_drops_source_dependencies_and_its_file(self):
        self.assertEqual(self.wrapped(project_n.xmake_env(Path("build/x"))),
                         ["-c", "a b.cpp", "-Fosrc/sourceDependencies.obj", "-nologo"])

    @unittest.skipIf(os.name == "nt", "bash script")
    def test_the_wrapper_passes_everything_through_for_a_checkout_without_it(self):
        # The MSVC install is shared by every checkout; one whose xmake.lua keeps the build cache on needs the JSON.
        env = {k: v for k, v in os.environ.items() if k != "PROJECT_N_CL_DROP_SOURCE_DEPENDENCIES"}
        self.assertEqual(self.wrapped(env), list(self.ARGS))

    @unittest.skipIf(os.name == "nt", "symlinks")
    def test_the_wrapper_is_installed_over_the_symlink_once(self):
        with tempfile.TemporaryDirectory() as tmp:
            bin_dir = Path(tmp) / "bin" / "x64"
            bin_dir.mkdir(parents=True)
            (bin_dir / "cl").write_text("#!/usr/bin/env bash\n")
            (bin_dir / "cl.exe").symlink_to("cl")
            project_n.install_cl_wrapper(Path(tmp))
            wrapper = bin_dir / "cl.exe"
            self.assertFalse(wrapper.is_symlink())
            self.assertEqual(wrapper.read_text(), project_n.CL_WRAPPER)
            self.assertTrue(os.access(wrapper, os.X_OK))
            os.utime(wrapper, (1, 1))
            project_n.install_cl_wrapper(Path(tmp))
            self.assertEqual(wrapper.stat().st_mtime, 1)


class ArtifactReferenceTest(unittest.TestCase):
    def references_in_artifact(self, preset):
        """The presets `artifact` compares pictures for, stopped at the step after the comparison."""
        class Stop(Exception):
            pass

        def stop(*args):
            raise Stop

        compared = []
        with mock.patch.multiple(project_n, IN_CONTAINER=True, tree_dirty=lambda: "", build=lambda *args: Path("build"),
                                 run=lambda *args, **kwargs: None, xmake_env=lambda bdir: {},
                                 run_references=compared.append, binary_path=stop), \
             mock.patch.object(project_n.shutil, "rmtree"), self.assertRaises(Stop):
            project_n.cmd_artifact(project_n.argparse.Namespace(preset=preset, reuse_build=False))
        return compared

    def test_a_linux_artefact_compares_the_reference_pictures(self):
        self.assertEqual(self.references_in_artifact("linux-gcc"), ["linux-gcc"])

    def test_a_windows_artefact_doesnt(self):
        self.assertEqual(self.references_in_artifact("windows-msvc"), [])

    def test_artifact_takes_the_reuse_option(self):
        self.assertTrue(project_n.make_parser().parse_args(["artifact", "--preset", "macos-clang", "--reuse-build"]).reuse_build)
        self.assertFalse(project_n.make_parser().parse_args(["artifact", "--preset", "macos-clang"]).reuse_build)


class ObjectCacheTest(unittest.TestCase):
    """CI's object cache: warm on branch pushes, never read on main or a tag (the user's cold rule)."""

    def setUp(self):
        self.tmp = Path(self.enterContext(tempfile.TemporaryDirectory()))
        self.ran = []
        self.enterContext(mock.patch.multiple(project_n, ROOT=self.tmp, IN_CONTAINER=False, xmake_env=lambda bdir: {},
                                              run=lambda cmd, **kwargs: self.ran.append([str(c) for c in cmd])))

    def ci(self, cache=None, ref=None):
        """os.environ as CI sets it: the object cache folder and the ref, or neither (a local run). Never CI's step
        output (GITHUB_OUTPUT): a test run on a runner would write into the real step's."""
        env = {k: v for k, v in os.environ.items() if k not in (project_n.BUILD_CACHE_ENV, "GITHUB_REF", "GITHUB_OUTPUT")}
        env |= {project_n.BUILD_CACHE_ENV: str(cache)} if cache else {}
        env |= {"GITHUB_REF": ref} if ref else {}
        return mock.patch.dict(project_n.os.environ, env, clear=True)

    def configured(self, preset="linux-clang", mode="release"):
        """The `xmake f` command `configure` runs."""
        self.ran.clear()
        project_n.configure(preset, mode)
        return next(cmd for cmd in self.ran if cmd[1:2] == ["f"])

    def test_no_cache_without_the_variable(self):
        with self.ci(ref="refs/heads/ci-speed"):
            self.assertIsNone(project_n.object_cache_dir("linux-clang", "release"))
            # Locally nothing changes: XMake's own cache in the build folder stays on.
            self.assertFalse([a for a in self.configured() if a.startswith(("--build_cache", "--ccachedir", "--ccache"))])

    def test_a_relative_cache_folder_is_made_absolute(self):
        # podman would take a relative source for a named volume, and XMake resolves --ccachedir from ROOT.
        with self.ci("rel-cache", "refs/heads/ci-speed"), mock.patch.object(project_n.Path, "mkdir"):
            self.assertEqual(project_n.object_cache_dir("linux-clang", "release"),
                             Path.cwd() / "rel-cache" / "linux-clang-release")
            cmd = project_n.container_cmd("image", ["true"])
        self.assertIn(f"{Path.cwd() / 'rel-cache'}:/home/dev/build-cache:Z", cmd)

    def test_a_branch_build_uses_its_leaf_folder(self):
        with self.ci(self.tmp / "c", "refs/heads/ci-speed"):
            folder = self.tmp / "c" / "linux-clang-release"
            self.assertEqual(project_n.object_cache_dir("linux-clang", "release"), folder)
            self.assertEqual(project_n.object_cache_dir("linux-clang", "debug", "address", unity=True),
                             self.tmp / "c" / "linux-clang-debug-address-unity")
            cmd = self.configured()
        self.assertIn("--build_cache=y", cmd)
        self.assertIn(f"--ccachedir={folder}", cmd)
        self.assertTrue(folder.is_dir())

    def test_msvc_stays_uncached(self):
        with self.ci("/c", "refs/heads/ci-speed"):
            self.assertIsNone(project_n.object_cache_dir("windows-msvc", "release"))

    def test_gcc_stays_uncached(self):
        # GCC's preprocessed text names the precompiled header instead of holding it, so XMake's key misses a header change.
        with self.ci("/c", "refs/heads/ci-speed"):
            self.assertIsNone(project_n.object_cache_dir("linux-gcc", "release"))

    def test_a_tag_uses_no_cache(self):
        with self.ci("/c", "refs/tags/v0.4.0"):
            self.assertIsNone(project_n.object_cache_dir("linux-clang", "release"))
            self.assertTrue(project_n.cold_ref() and project_n.tag_ref())

    def test_main_writes_only_into_an_empty_folder(self):
        folder = self.tmp / "c" / "linux-clang-release"
        with self.ci(self.tmp / "c", "refs/heads/main"):
            self.assertTrue(project_n.cold_ref() and not project_n.tag_ref())
            self.assertIn("--build_cache=y", self.configured())  # no folder yet: nothing to read
            self.assertIn("--build_cache=y", self.configured())  # still empty
            (folder / "ab").mkdir()
            (folder / "ab" / "object").write_bytes(b"cached")
            cmd = self.configured()
        self.assertFalse([a for a in cmd if a.startswith(("--build_cache", "--ccachedir"))])
        self.assertIn("--ccache=n", cmd)  # nor XMake's own cache in the build folder (on in containers: no CI there)
        with self.ci(ref="refs/heads/main"):
            self.assertIn("--ccache=n", self.configured())
        with self.ci(self.tmp / "c", "refs/tags/v0.4.0"):
            self.assertIn("--ccache=n", self.configured())

    def test_a_cold_ref_refuses_a_reused_build_folder(self):
        bdir = project_n.build_dir("linux-clang", "release")
        with self.ci(ref="refs/heads/main"):
            project_n.build("linux-clang", "release")  # no folder
            (self.tmp / bdir / ".objs").mkdir(parents=True)
            project_n.build("linux-clang", "release")  # cleaned: no objects left
            (self.tmp / bdir / ".objs" / "core" / "x.o").parent.mkdir(parents=True)
            (self.tmp / bdir / ".objs" / "core" / "x.o").write_bytes(b"o")
            self.ran.clear()
            with self.assertRaisesRegex(project_n.Fail, re.escape(str(bdir))):
                project_n.build("linux-clang", "release")
            self.assertEqual(self.ran, [])
            (self.tmp / bdir / ".objs" / "core" / "x.o").unlink()
            project_n.build("linux-clang", "release")
            (self.tmp / bdir / ".build_cache" / "ab").mkdir(parents=True)  # XMake's own cache, kept from a build
            (self.tmp / bdir / ".build_cache" / "ab" / "object").write_bytes(b"o")
            with self.assertRaisesRegex(project_n.Fail, re.escape(str(bdir))):
                project_n.build("linux-clang", "release")
        with self.ci(ref="refs/heads/ci-speed"):
            project_n.build("linux-clang", "release")  # a branch build reuses its folder as ever

    def test_the_container_gets_the_cache_and_the_ref(self):
        with self.ci("/home/runner/build-cache", "refs/heads/ci-speed"), mock.patch.object(project_n.Path, "mkdir"):
            cmd = project_n.container_cmd("image", ["true"])
        self.assertIn(f"{Path('/home/runner/build-cache').absolute()}:/home/dev/build-cache:Z", cmd)
        self.assertIn("PROJECT_N_BUILD_CACHE=/home/dev/build-cache", cmd)
        self.assertIn("GITHUB_REF=refs/heads/ci-speed", cmd)
        with self.ci():
            cmd = project_n.container_cmd("image", ["true"])
        self.assertFalse([a for a in cmd if "build-cache" in a or "GITHUB_REF" in a])
        with self.ci(self.tmp / "c", "refs/tags/v1.0.0"):  # a tag neither reads nor writes one
            cmd = project_n.container_cmd("image", ["true"])
        self.assertFalse([a for a in cmd if "build-cache" in a])
        self.assertIn("GITHUB_REF=refs/tags/v1.0.0", cmd)
        self.assertFalse((self.tmp / "c").exists())

    def test_artifact_on_a_tag_builds_without_a_cache(self):
        class Stop(Exception):
            pass

        def stop(*args):
            raise Stop

        with self.ci(self.tmp / "c", "refs/tags/v1.0.0"), \
             mock.patch.multiple(project_n, IN_CONTAINER=True, tree_dirty=lambda: "", lfs_pointers=lambda folder: [],
                                 run_references=lambda preset: None, binary_path=stop), self.assertRaises(Stop):
            project_n.cmd_artifact(project_n.argparse.Namespace(preset="linux-clang", reuse_build=False))
        xmake_f = next(cmd for cmd in self.ran if cmd[1:2] == ["f"])
        self.assertFalse([a for a in xmake_f if a.startswith(("--build_cache", "--ccachedir"))])
        self.assertFalse((self.tmp / "c").exists())  # a tag writes no cache either

    def artifact_cleans(self, ref, reuse_build):
        """Whether `artifact` deletes the release build folder first (its clean room), stopped after the build."""
        class Stop(Exception):
            pass

        def stop(*args):
            raise Stop

        deleted = []
        with self.ci(ref=ref), \
             mock.patch.multiple(project_n, IN_CONTAINER=True, tree_dirty=lambda: "", lfs_pointers=lambda folder: [],
                                 run_references=lambda preset: None, binary_path=stop), \
             mock.patch.object(project_n.shutil, "rmtree", lambda path, **kwargs: deleted.append(Path(path))):
            bdir = project_n.build_dir("linux-clang", "release")
            (self.tmp / bdir / ".objs").mkdir(parents=True, exist_ok=True)
            with mock.patch.object(project_n, "build", lambda *args: bdir), self.assertRaises(Stop):
                project_n.cmd_artifact(project_n.argparse.Namespace(preset="linux-clang", reuse_build=reuse_build))
        return self.tmp / bdir in deleted

    def test_artifact_reuses_the_release_build_on_a_branch_only(self):
        # A branch push may package the build its `test --mode release` step made (macOS: one build fewer).
        self.assertFalse(self.artifact_cleans("refs/heads/ci-speed", reuse_build=True))
        self.assertTrue(self.artifact_cleans("refs/heads/ci-speed", reuse_build=False))
        self.assertTrue(self.artifact_cleans(None, reuse_build=False))
        # Main and tags refuse the reuse: their artefacts compile from source in a clean room (the cold rule).
        self.assertTrue(self.artifact_cleans("refs/heads/main", reuse_build=True))
        self.assertTrue(self.artifact_cleans("refs/tags/v1.0.0", reuse_build=True))

    def test_artifact_hands_the_reuse_to_the_container(self):
        with self.ci(ref="refs/heads/ci-speed"), \
             mock.patch.multiple(project_n, tree_dirty=lambda: "", lfs_pointers=lambda folder: [],
                                 reenter=lambda preset, argv: self.ran.append([preset, *argv])):
            project_n.cmd_artifact(project_n.argparse.Namespace(preset="linux-gcc", reuse_build=True))
            project_n.cmd_artifact(project_n.argparse.Namespace(preset="linux-gcc", reuse_build=False))
        self.assertEqual(self.ran, [["linux-gcc", "artifact", "--preset", "linux-gcc", "--reuse-build"],
                                    ["linux-gcc", "artifact", "--preset", "linux-gcc"]])


class PruneBuildCacheTest(unittest.TestCase):
    """Before CI saves its object cache folder: only what this run's builds used stays, and an empty folder is never
    saved (XMake's build cache never evicts, so each save grew)."""

    def setUp(self):
        self.tmp = Path(self.enterContext(tempfile.TemporaryDirectory()))
        self.cache = self.tmp / "build-cache"
        self.output = self.tmp / "github-output"
        self.enterContext(mock.patch.multiple(project_n, ROOT=self.tmp, IN_CONTAINER=False))
        self.enterContext(self.ci())

    def ci(self, ref="refs/heads/ci-speed", cache=True):
        env = {k: v for k, v in os.environ.items() if k not in (project_n.BUILD_CACHE_ENV, "GITHUB_REF", "GITHUB_OUTPUT")}
        env |= {"GITHUB_REF": ref, "GITHUB_OUTPUT": str(self.output)}
        env |= {project_n.BUILD_CACHE_ENV: str(self.cache)} if cache else {}
        return mock.patch.dict(project_n.os.environ, env, clear=True)

    def obj(self, bdir, name, data):
        path = self.tmp / bdir / ".objs" / "core" / "linux" / "x86_64" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def cached(self, folder, key, data, info=False):
        """A file XMake's build cache keeps: <folder>/<key[:2]>/<key>, with its warnings in <key>.txt."""
        path = self.cache / folder / key[:2] / key
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        if info:
            path.with_name(key + ".txt").write_text("{outdata = 'warning'}")
        return path

    def tidy(self, name, age):
        """A clang-tidy result `age` seconds old (a restored archive keeps its files' old times)."""
        path = self.cache / "tidy" / f"{name}.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('{"output": "", "returncode": 0}')
        stamp = time.time() - age
        os.utime(path, (stamp, stamp))
        return path

    def prune(self):
        with contextlib.redirect_stdout(io.StringIO()) as out:
            project_n.cmd_prune_build_cache(argparse.Namespace())
        return out.getvalue()

    def saved(self):
        return self.output.read_text().splitlines()[-1]

    def files(self):
        return sorted(p.relative_to(self.cache).as_posix() for p in self.cache.rglob("*") if p.is_file())

    def test_only_what_this_runs_objects_match_stays(self):
        # Containers build under build/container/, native builds (macOS) under build/; both feed <preset>-<leaf>.
        self.obj("build/container/linux-clang/release", "a.cpp.o", b"object a")
        self.obj("build/macos-clang/debug-address", "b.cpp.o", b"object b")
        self.cached("linux-clang-release", "aa11", b"object a", info=True)
        self.cached("linux-clang-release", "bb22", b"stale object", info=True)
        self.cached("macos-clang-debug-address", "cc33", b"object b")
        self.cached("macos-clang-debug-address", "dd44", b"object a")  # another build's object counts for nothing
        (self.cache / "linux-clang-release" / "ee").mkdir()
        (self.cache / "linux-clang-release" / "ee" / "ee55.txt").write_text("{}")  # warnings without their object
        out = self.prune()
        self.assertEqual(self.files(), ["linux-clang-release/aa/aa11", "linux-clang-release/aa/aa11.txt",
                                        "macos-clang-debug-address/cc/cc33"])
        self.assertFalse((self.cache / "linux-clang-release" / "bb").exists())  # emptied folders go too
        self.assertEqual(self.saved(), "save=true")
        self.assertEqual(len(out.splitlines()), 1, out)  # one summary line: outcomes, not steps
        self.assertIn("kept 3 object cache files", out)  # aa11 and its warnings, cc33
        self.assertIn("removed 4", out)

    def test_a_folder_no_preset_owns_is_removed(self):
        self.obj("build/container/linux-clang/release", "a.cpp.o", b"object a")
        self.cached("old-layout", "aa11", b"object a")
        self.prune()
        self.assertEqual(self.files(), [])
        self.assertEqual(self.saved(), "save=false")

    def test_main_keeps_every_object(self):
        # Main wrote into an empty folder (the cold rule), so all of it is this run's; its clean-room rebuild (macOS
        # artefact) compiles without the cache, and those objects need not match the ones its tests' build cached.
        self.cached("macos-clang-release", "aa11", b"object a", info=True)
        with self.ci("refs/heads/main"):
            self.prune()
        self.assertEqual(self.files(), ["macos-clang-release/aa/aa11", "macos-clang-release/aa/aa11.txt"])
        self.assertEqual(self.saved(), "save=true")

    def test_clang_tidy_results_stay_when_used_or_written_this_run(self):
        old = self.tidy("old", age=86400)
        used = self.tidy("used", age=86400)
        project_n.mark_tidy_run(self.cache / "tidy")
        os.utime(used)  # a hit marks it (tidy_one)
        self.tidy("written", age=0)
        self.prune()
        self.assertEqual(self.files(), ["tidy/used.json", "tidy/written.json"])  # the mark goes too: a fresh one per run
        self.assertFalse(old.exists())
        self.assertEqual(self.saved(), "save=true")

    def test_clang_tidy_results_no_lint_read_go(self):
        # No mark: lint stopped before clang-tidy (no changed C++), so this run used none of them.
        self.tidy("restored", age=86400)
        self.prune()
        self.assertEqual(self.files(), [])
        self.assertEqual(self.saved(), "save=false")

    def test_an_empty_folder_is_never_saved(self):
        # Main merged no C++: lint returned before building, after container_cmd had made the folder.
        self.prune()
        self.assertEqual(self.saved(), "save=false")
        self.cache.mkdir()
        self.prune()
        self.assertEqual(self.saved(), "save=false")
        self.assertTrue(self.cache.is_dir())

    def test_without_a_cache_nothing_is_saved(self):
        with self.ci(cache=False):
            self.prune()
        self.assertEqual(self.saved(), "save=false")
        with self.ci("refs/tags/v1.0.0"):
            self.prune()
        self.assertEqual(self.saved(), "save=false")


class CiCachesTest(unittest.TestCase):
    """The cleanup job: per family of a ref's cache entries, the newest stays and the rest go."""

    HASH = "1e5cfaf136c8680a89cc73eba35ee0f011e76a470773e203055f820608aeaaeb"
    SHA = "0b2c7c3c8d81d9e0ddfefe34add4b052ff14f3c4"

    def setUp(self):
        self.calls, self.deleted, self.entries, self.refuse, self.refuse_after = [], [], [], None, 0
        self.enterContext(mock.patch.dict(project_n.os.environ, {"GITHUB_REF": "refs/heads/ci-speed"}))

        def capture(cmd, check=True):
            self.calls.append([str(c) for c in cmd])
            if self.refuse and cmd[2] == self.refuse and len(self.deleted) >= self.refuse_after:
                raise subprocess.CalledProcessError(1, cmd, stderr="HTTP 403: Resource not accessible by integration")
            if cmd[2] == "delete":
                self.deleted.append(int(cmd[3]))
                return ""
            return json.dumps(self.entries)

        self.enterContext(mock.patch.object(project_n, "capture", capture))

    def entry(self, key, created, version="v1", size=1000):
        self.entries.append({"id": len(self.entries) + 1, "key": key, "version": version, "sizeInBytes": size,
                             "createdAt": f"2026-11-14T{created}:00.000000Z"})
        return len(self.entries)

    def prune(self):
        with contextlib.redirect_stdout(io.StringIO()) as out:
            code = project_n.cmd_prune_ci_caches(argparse.Namespace())
        return code, out.getvalue()

    def test_families_from_the_keys_ci_writes(self):
        family = project_n.cache_family
        self.assertEqual(family(f"objects-container-linux-clang-{self.HASH}-{self.SHA}"), "objects-container-linux-clang")
        self.assertEqual(family(f"objects-lint-linux-clang-{self.HASH}-{self.SHA}"), "objects-lint-linux-clang")
        self.assertEqual(family(f"objects-macos-debug-address-{self.HASH}-{self.SHA}"), "objects-macos-debug-address")
        self.assertEqual(family(f"xmake-windows-msvc-{self.HASH}"), "xmake-windows-msvc")
        self.assertEqual(family(f"xmake-linux-clang-{self.HASH}"), "xmake-linux-clang")
        self.assertEqual(family(f"xmake-windows-native-{self.HASH}"), "xmake-windows-native")
        self.assertEqual(family("lfs-63a480f5e6a32a13"), "lfs")
        # Setup caches are never touched: the pixi envs, the MSVC install and toolset, the macOS XMake bundle.
        for key in (f"pixi-linux-64-{self.HASH}", f"msvc-{self.HASH}", f"msvc-install-{self.HASH}",
                    f"xmake-tool-macos-{self.HASH}", "objects-lint", "other"):
            self.assertIsNone(family(key), key)

    def test_the_newest_of_each_family_stays(self):
        old = self.entry(f"objects-lint-linux-clang-{self.HASH}-{'1' * 40}", "01:00")
        new = self.entry(f"objects-lint-linux-clang-{self.HASH}-{self.SHA}", "03:00")
        older_env = self.entry(f"objects-lint-linux-clang-{'f' * 64}-{'2' * 40}", "02:00")  # a toolchain bump ago
        xmake_old = self.entry(f"xmake-lint-{'f' * 64}", "01:30", size=2048)
        self.entry(f"xmake-lint-{self.HASH}", "02:30")
        # One key in two versions (paths, compression: the LFS action on Linux and Windows): both stay.
        self.entry("lfs-63a480f5e6a32a13", "01:00", version="linux")
        self.entry("lfs-63a480f5e6a32a13", "01:01", version="windows")
        lfs_old = self.entry("lfs-c1ce682b00a8f188", "00:30", version="linux")
        self.entry(f"msvc-install-{self.HASH}", "00:00")
        self.entry(f"msvc-install-{'f' * 64}", "00:10")
        self.entry(f"pixi-linux-64-{self.HASH}", "00:00")
        code, out = self.prune()
        self.assertEqual(code, 0)
        self.assertEqual(sorted(self.deleted), sorted([old, older_env, xmake_old, lfs_old]))
        self.assertNotIn(new, self.deleted)
        list_call = self.calls[0]
        self.assertEqual(list_call[:3], ["gh", "cache", "list"])
        self.assertIn("refs/heads/ci-speed", list_call)  # this ref's entries only
        self.assertTrue(all("--ref" in c or c[2] == "delete" for c in self.calls))
        self.assertEqual(len(out.splitlines()), 1, out)
        self.assertIn("deleted 4", out)

    def test_a_refusal_after_some_deletes_counts_them(self):
        # A 403 part-way through says how many went first, not "nothing deleted".
        self.entry(f"xmake-lint-{'e' * 64}", "01:00")
        self.entry(f"xmake-lint-{'f' * 64}", "01:30")
        self.entry(f"xmake-lint-{self.HASH}", "02:30")
        self.refuse, self.refuse_after = "delete", 1
        code, out = self.prune()
        self.assertEqual(code, 0)
        self.assertEqual(len(self.deleted), 1)
        self.assertEqual(len(out.splitlines()), 1, out)
        self.assertIn("deleted 1", out)
        self.assertNotIn("nothing deleted", out)

    def test_a_read_only_token_skips_quietly(self):
        # Dependabot's runs get a read-only token: deleting is refused, and the run must not fail for it.
        self.entry(f"xmake-lint-{'f' * 64}", "01:30")
        self.entry(f"xmake-lint-{self.HASH}", "02:30")
        self.refuse = "delete"
        code, out = self.prune()
        self.assertEqual(code, 0)
        self.assertEqual(len(out.splitlines()), 1, out)
        self.assertIn("403", out)
        self.refuse = "list"
        self.assertEqual(self.prune()[0], 0)

    def test_an_entry_already_gone_is_skipped(self):
        # Two runs of one ref may race to delete the same entry: the loser's HTTP 404 means it is gone already.
        gone = self.entry(f"xmake-lint-{'e' * 64}", "01:00")
        old = self.entry(f"xmake-lint-{'f' * 64}", "01:30")
        self.entry(f"xmake-lint-{self.HASH}", "02:30")

        def capture(cmd, check=True):
            if cmd[2] == "delete" and int(cmd[3]) == gone:
                raise subprocess.CalledProcessError(1, cmd, stderr="HTTP 404: Not Found")
            if cmd[2] == "delete":
                self.deleted.append(int(cmd[3]))
                return ""
            return json.dumps(self.entries)

        with mock.patch.object(project_n, "capture", capture):
            code, out = self.prune()
        self.assertEqual(code, 0)
        self.assertEqual(self.deleted, [old])
        self.assertIn("deleted 1", out)

    def test_another_failure_fails(self):
        self.entry(f"xmake-lint-{'f' * 64}", "01:30")
        self.entry(f"xmake-lint-{self.HASH}", "02:30")

        def capture(cmd, check=True):
            if cmd[2] == "delete":
                raise subprocess.CalledProcessError(1, cmd, stderr="HTTP 500: server error")
            return json.dumps(self.entries)

        with mock.patch.object(project_n, "capture", capture), self.assertRaises(subprocess.CalledProcessError):
            self.prune()

    def test_ci_only(self):
        with mock.patch.dict(project_n.os.environ, {"GITHUB_REF": ""}), self.assertRaises(project_n.Fail):
            self.prune()


class RunTest(unittest.TestCase):
    def test_run_hands_the_arguments_after_the_separator_to_the_game(self):
        ran = []
        with mock.patch.multiple(project_n, build=lambda *args: Path("build"), xmake_env=lambda bdir: {},
                                 run=lambda argv, env: ran.append(argv)):
            project_n.cmd_run(project_n.argparse.Namespace(preset=project_n.NATIVE_PRESET, mode="debug", sanitizer="none",
                                                           debug=False, args=["--", "--painted"]))
        self.assertEqual(ran, [["xmake", "run", project_n.APP, "--painted"]])


class ProfileTest(unittest.TestCase):
    class Recorder:
        """A tracy-capture that writes part of its file at once and, as `behaviour` says, finishes it
        ("saves"), exits 1 ("fails") or never exits ("hangs") once the game has quit."""
        def __init__(self, argv, behaviour):
            self.argv, self.behaviour, self.returncode, self.terminated = argv, behaviour, None, False
            Path(argv[-1]).write_bytes(b"partial")

        def poll(self):
            return self.returncode

        def wait(self, timeout=None):
            if self.terminated:
                return
            if self.behaviour == "hangs":
                raise subprocess.TimeoutExpired(self.argv, timeout)
            self.returncode = 0 if self.behaviour == "saves" else 1

        def terminate(self):
            self.terminated, self.returncode = True, -15

    def profile(self, game=lambda argv: None, seconds=10, behaviour="saves"):
        """Runs `profile` with `game` in place of `run`; keeps the recorders, the game's command lines
        and the captures left in build/profiles/."""
        self.ran, self.recorders, self.left = [], [], []

        def recorder(argv, cwd):
            self.recorders.append(self.Recorder(argv, behaviour))
            return self.recorders[-1]

        def run(argv, env):
            self.ran.append(argv)
            game(argv)
        with tempfile.TemporaryDirectory() as tmp, \
             mock.patch.multiple(project_n, ROOT=Path(tmp), build=lambda *args: Path("build"), xmake_env=lambda bdir: {}, run=run,
                                 pixi_prefix=lambda name: Path("/profiler")), \
             mock.patch.object(project_n.shutil, "which", lambda name, path: "/profiler/bin/tracy-capture"), \
             mock.patch.object(project_n.subprocess, "Popen", recorder), contextlib.redirect_stdout(io.StringIO()):
            try:
                project_n.cmd_profile(project_n.argparse.Namespace(preset=project_n.NATIVE_PRESET, seconds=seconds,
                                                                   args=["--", "--stress"]))
            finally:
                self.left = [p.name for p in (Path(tmp) / "build" / "profiles").glob("*")]

    def test_the_game_quits_by_itself_and_the_recorder_saves_into_build_profiles(self):
        self.profile(seconds=5)
        self.assertEqual(self.ran, [["xmake", "run", project_n.APP, "--quit-after", "5", "--stress"]])
        recorded = Path(self.recorders[0].argv[-1])
        self.assertEqual((self.recorders[0].argv[:2], recorded.parent.parts[-2:], recorded.suffix),
                         (["/profiler/bin/tracy-capture", "-o"], ("build", "profiles"), ".tracy"))
        self.assertEqual(self.left, [recorded.name])

    def test_a_game_that_fails_stops_the_recorder_and_leaves_no_capture(self):
        def fail(argv):
            raise subprocess.CalledProcessError(1, argv)
        with self.assertRaises(subprocess.CalledProcessError):
            self.profile(fail)
        self.assertTrue(self.recorders[0].terminated)
        self.assertEqual(self.left, [])

    def test_a_recorder_that_fails_leaves_no_capture(self):
        with self.assertRaisesRegex(project_n.Fail, "status 1"):
            self.profile(behaviour="fails")
        self.assertEqual(self.left, [])

    def test_a_recorder_that_doesnt_finish_is_stopped_and_leaves_no_capture(self):
        with self.assertRaisesRegex(project_n.Fail, "within 60 s"):
            self.profile(behaviour="hangs")
        self.assertTrue(self.recorders[0].terminated)
        self.assertEqual(self.left, [])

    def test_seconds_below_one_are_refused_before_the_build(self):
        with self.assertRaises(project_n.Fail):
            self.profile(seconds=0)
        self.assertEqual(self.recorders, [])

    def test_profiler_opens_a_saved_capture_by_its_full_path(self):
        ran = []
        with mock.patch.object(project_n, "run", ran.append):
            project_n.cmd_profiler(project_n.argparse.Namespace(file=None))
            project_n.cmd_profiler(project_n.argparse.Namespace(file="a.tracy"))
        self.assertEqual([argv[4:] for argv in ran], [["tracy-profiler"], ["tracy-profiler", Path("a.tracy").resolve()]])


class SourceTest(unittest.TestCase):
    def test_generated_headers_arent_sources(self):
        self.assertTrue(project_n.is_source("src/core/log.hpp"))
        self.assertFalse(project_n.is_source("src/core/generated/atlas.hpp"))


class ReleaseTest(unittest.TestCase):
    CHANGELOG = "# Changelog\n\n## 0.2.0 - 2026-11-01\n\n### Features\n\n- Log at six levels\n\n## 0.1.0 - 2026-10-30\n\n- Older\n"

    def test_the_notes_hold_only_that_versions_section(self):
        notes = project_n.release_notes(self.CHANGELOG, "0.2.0")
        self.assertTrue(notes.startswith("### Features\n\n- Log at six levels\n\n"))
        self.assertNotIn("Older", notes)

    def test_the_notes_say_the_builds_are_unsigned(self):
        self.assertIn(project_n.UNSIGNED_NOTE, project_n.release_notes(self.CHANGELOG, "0.1.0"))

    def test_a_version_without_a_section_fails(self):
        with self.assertRaisesRegex(project_n.Fail, "0.3.0"):
            project_n.release_notes(self.CHANGELOG, "0.3.0")

    def test_a_missing_archive_is_named(self):
        files = {f"dist/project_n-0.2.0-{p}{part}.zip": "" for p in project_n.PRESETS for part in ("", "-symbols")}
        del files["dist/project_n-0.2.0-macos-clang.zip"]
        with temp_repo(files), self.assertRaisesRegex(project_n.Fail, r"missing release archives:\nproject_n-0.2.0-macos-clang.zip$"):
            project_n.release_archives("0.2.0")

    def test_publish_refuses_a_commit_without_the_versions_tag(self):
        with temp_repo({"xmake.lua": 'set_version("0.2.0")\n'}), mock.patch.object(project_n, "run") as run, \
                self.assertRaisesRegex(project_n.Fail, "v0.2.0"):
            project_n.cmd_publish(None)
        run.assert_not_called()

    def test_tagging_refuses_a_main_without_the_release_commit(self):
        with temp_repo({"xmake.lua": 'set_version("0.2.0")\n'}), mock.patch.object(project_n, "run") as run, \
                self.assertRaisesRegex(project_n.Fail, r"not 'chore\(release\): v0.2.0'"):
            project_n.cmd_release(argparse.Namespace(tag=True, dry_run=False))
        run.assert_called_once_with(["git", "fetch", "--quiet"])

    @contextlib.contextmanager
    def release_commit(self, changelog, cliff_output):
        """A repository whose last commit is v0.2.0's release, with git-cliff answering cliff_output."""
        real = project_n.capture
        fake = lambda cmd, check=True: cliff_output if cmd[:2] == ["git", "cliff"] else real(cmd, check)
        with temp_repo({"xmake.lua": 'set_version("0.2.0")\n', "CHANGELOG.md": changelog}) as root, \
                mock.patch.object(project_n, "capture", fake), mock.patch.object(project_n, "run") as run:
            git(root, "commit", "-q", "--allow-empty", "--no-verify", "-m", "chore(release): v0.2.0")
            yield run

    def test_tagging_tags_the_release_commit(self):
        with self.release_commit("## 0.2.0\n", "## 0.2.0\n") as run, contextlib.redirect_stdout(io.StringIO()):
            project_n.cmd_release(argparse.Namespace(tag=True, dry_run=False))
        run.assert_called_with(["git", "tag", "-a", "v0.2.0", "-m", "v0.2.0"])

    def test_tagging_refuses_a_changelog_behind_the_commits(self):
        with self.release_commit("## 0.2.0\n", "## 0.2.0\n- A feature merged after release\n") as run, \
                self.assertRaisesRegex(project_n.Fail, "CHANGELOG.md no longer matches"):
            project_n.cmd_release(argparse.Namespace(tag=True, dry_run=False))
        self.assertNotIn(mock.call(["git", "tag", "-a", "v0.2.0", "-m", "v0.2.0"]), run.call_args_list)

    def test_tagging_ignores_the_date_git_cliff_puts_on_today(self):
        with self.release_commit("## 0.2.0 - 2026-10-31\n", "## 0.2.0 - 2026-11-01\n") as run, contextlib.redirect_stdout(io.StringIO()):
            project_n.cmd_release(argparse.Namespace(tag=True, dry_run=False))
        run.assert_called_with(["git", "tag", "-a", "v0.2.0", "-m", "v0.2.0"])

    def test_release_rejects_tag_with_dry_run(self):
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            project_n.make_parser().parse_args(["release", "--tag", "--dry-run"])

    def test_release_refuses_a_leftover_release_branch(self):
        real = project_n.capture
        fake = lambda cmd, check=True: "v0.2.0" if "--bumped-version" in cmd else real(cmd, check)
        with temp_repo({"xmake.lua": 'set_version("0.1.0")\n'}) as root, mock.patch.object(project_n, "capture", fake), \
                mock.patch.object(project_n, "run") as run, self.assertRaisesRegex(project_n.Fail, "release/v0.2.0 already exists"):
            git(root, "branch", "release/v0.2.0")
            project_n.cmd_release(argparse.Namespace(tag=False, dry_run=False))
        run.assert_called_once_with(["git", "fetch", "--quiet"])

    def test_release_commits_on_its_own_branch(self):
        real = project_n.capture
        fake = lambda cmd, check=True: "v0.2.0" if "--bumped-version" in cmd else real(cmd, check)
        with temp_repo({"xmake.lua": 'set_version("0.1.0")\n'}), mock.patch.object(project_n, "capture", fake), \
                mock.patch.object(project_n, "run") as run, contextlib.redirect_stdout(io.StringIO()):
            project_n.cmd_release(argparse.Namespace(tag=False, dry_run=False))
        calls = [c.args[0] for c in run.call_args_list]
        self.assertLess(calls.index(["git", "switch", "-c", "release/v0.2.0"]), calls.index(["git", "commit", "-m", "chore(release): v0.2.0"]))

    def test_publish_drafts_the_release_with_every_archive(self):
        archives = [f"dist/project_n-0.2.0-{p}{part}.zip" for p in project_n.PRESETS for part in ("", "-symbols")]
        files = {"xmake.lua": 'set_version("0.2.0")\n', "CHANGELOG.md": self.CHANGELOG, **dict.fromkeys(archives, "")}
        with temp_repo(files) as root, mock.patch.object(project_n, "run") as run:
            git(root, "tag", "v0.2.0")
            project_n.cmd_publish(None)
        cmd = run.call_args.args[0]
        self.assertEqual(cmd[:6], ["gh", "release", "create", "v0.2.0", "--verify-tag", "--draft"])
        self.assertEqual([Path(a).relative_to(root).as_posix() for a in cmd[-len(archives):]], archives)

    def test_ci_gets_the_artifact_name_as_a_step_output(self):
        with temp_repo({"xmake.lua": 'set_version("0.2.0")\n'}) as root, \
                mock.patch.dict(project_n.os.environ, {"GITHUB_REF": "refs/tags/v0.2.0", "GITHUB_OUTPUT": str(root / "out")}):
            project_n.write_ci_artifact_name("linux-gcc")
            self.assertEqual((root / "out").read_text(), "archive=project_n-0.2.0-linux-gcc\n")

    def test_ci_names_a_tag_build_after_its_version(self):
        with temp_repo({"xmake.lua": 'set_version("0.2.0")\n'}), mock.patch.dict(project_n.os.environ, {"GITHUB_REF": "refs/tags/v0.2.0"}):
            self.assertEqual(project_n.ci_artifact_name("linux-gcc"), "project_n-0.2.0-linux-gcc")

    def test_ci_names_a_dev_build_after_its_version_and_commit(self):
        with temp_repo({"xmake.lua": 'set_version("0.2.0")\n'}) as root, mock.patch.dict(project_n.os.environ, {"GITHUB_REF": "refs/heads/main"}):
            sha = subprocess.run(["git", "rev-parse", "--short=7", "HEAD"], cwd=root, capture_output=True, text=True, check=True).stdout.strip()
            self.assertEqual(project_n.ci_artifact_name("linux-gcc"), f"project_n-0.2.0-linux-gcc-{sha}")


class PinnedXmakeTest(unittest.TestCase):
    def test_a_missing_set_xmakever_is_reported(self):
        with temp_repo({"xmake.lua": 'set_version("0.1.0")\n'}), self.assertRaisesRegex(project_n.Fail, "set_xmakever"):
            project_n.pinned_xmake()


class DependencyTest(unittest.TestCase):
    def test_a_third_party_include_in_a_header_is_reported(self):
        with temp_repo({"src/window.hpp": "#pragma once\n#include <SDL3/SDL.h>\n"}):
            self.assertEqual(project_n.leaked_dependencies(["src/window.hpp"]), ["src/window.hpp:2: #include <SDL3/SDL.h>"])

    def test_a_platform_header_is_reported(self):
        with temp_repo({"src/window.hpp": "#pragma once\n#include <windows.h>\n"}):
            self.assertEqual(project_n.leaked_dependencies(["src/window.hpp"]), ["src/window.hpp:2: #include <windows.h>"])

    def test_a_quoted_third_party_include_is_reported(self):
        with temp_repo({"src/window.hpp": '#pragma once\n#include "SDL3/SDL.h"\n'}):
            self.assertEqual(project_n.leaked_dependencies(["src/window.hpp"]), ['src/window.hpp:2: #include "SDL3/SDL.h"'])

    def test_standard_and_own_headers_pass(self):
        with temp_repo({"src/core/a.hpp": "#pragma once\n",
                        "src/core/b.hpp": '#pragma once\n#include "core/a.hpp"\n#include "a.hpp"\n#include <cstdint>\n#include <print>\n'}):
            self.assertEqual(project_n.leaked_dependencies(["src/core/b.hpp"]), [])

    def test_a_facade_includes_its_library(self):
        with temp_repo({"src/core/log.hpp": "#pragma once\n#include <quill/Logger.h>\n"}):
            self.assertEqual(project_n.leaked_dependencies(["src/core/log.hpp"]), [])

    def test_a_hidden_facade_header_including_its_library_is_reported(self):
        with temp_repo({"src/core/jobs.hpp": "#pragma once\n#include <enkiTS/TaskScheduler.h>\n"}):
            self.assertEqual(project_n.leaked_dependencies(["src/core/jobs.hpp"]),
                             ["src/core/jobs.hpp:2: #include <enkiTS/TaskScheduler.h>"])

    def test_a_facade_including_another_library_is_reported(self):
        with temp_repo({"src/core/log.hpp": "#pragma once\n#include <tracy/Tracy.hpp>\n"}):
            self.assertEqual(project_n.leaked_dependencies(["src/core/log.hpp"]), ["src/core/log.hpp:2: #include <tracy/Tracy.hpp>"])


    def test_an_internal_header_includes_its_library(self):
        with temp_repo({"src/gfx/internal/sdl.hpp": "#pragma once\n#include <SDL3/SDL.h>\n"}):
            self.assertEqual(project_n.leaked_dependencies(["src/gfx/internal/sdl.hpp"]), [])


class ModuleTest(unittest.TestCase):
    def test_a_module_includes_a_module_below_it(self):
        with temp_repo({"src/platform/window.cpp": '#include "core/log.hpp"\n#include "platform/window.hpp"\n'}):
            self.assertEqual(project_n.crossed_modules(["src/platform/window.cpp"]), [])

    def test_a_module_including_one_above_it_is_reported(self):
        with temp_repo({"src/core/a.cpp": '#include "platform/window.hpp"\n'}):
            self.assertEqual(project_n.crossed_modules(["src/core/a.cpp"]), ['src/core/a.cpp:1: #include "platform/window.hpp"'])

    def test_game_including_platform_is_reported(self):
        with temp_repo({"src/game/a.hpp": '#pragma once\n#include "platform/window.hpp"\n'}):
            self.assertEqual(project_n.crossed_modules(["src/game/a.hpp"]), ['src/game/a.hpp:2: #include "platform/window.hpp"'])

    def test_another_modules_internal_header_is_reported(self):
        with temp_repo({"src/main.cpp": '#include "gfx/internal/sdl.hpp"\n', "tests/unit/gfx/a.cpp": '#include "gfx/internal/sdl.hpp"\n'}):
            self.assertEqual(project_n.crossed_modules(["src/main.cpp", "tests/unit/gfx/a.cpp"]),
                             ['src/main.cpp:1: #include "gfx/internal/sdl.hpp"', 'tests/unit/gfx/a.cpp:1: #include "gfx/internal/sdl.hpp"'])

    def test_a_modules_own_internal_header_passes(self):
        with temp_repo({"src/gfx/device.cpp": '#include "gfx/internal/sdl.hpp"\n#include "internal/sdl.hpp"\n'}):
            self.assertEqual(project_n.crossed_modules(["src/gfx/device.cpp"]), [])

    def test_main_and_tests_include_any_public_header(self):
        with temp_repo({"src/main.cpp": '#include "gfx/renderer.hpp"\n', "tests/unit/game/a.cpp": '#include "platform/window.hpp"\n'}):
            self.assertEqual(project_n.crossed_modules(["src/main.cpp", "tests/unit/game/a.cpp"]), [])


class PlatformMathsTest(unittest.TestCase):
    def test_platform_maths_in_core_and_game_are_reported(self):
        with temp_repo({"src/core/a.cpp": "f64 a = std::sin(x);\nf64 c = ::exp(x);\n", "src/game/b.hpp": "f32 b = powf(x, y);\n"}):
            self.assertEqual(project_n.platform_maths(["src/core/a.cpp", "src/game/b.hpp"]),
                             ["src/core/a.cpp:1: f64 a = std::sin(x);", "src/core/a.cpp:2: f64 c = ::exp(x);",
                              "src/game/b.hpp:1: f32 b = powf(x, y);"])

    def test_our_maths_logging_and_other_modules_pass(self):
        with temp_repo({"src/core/a.cpp": 'f64 a = math::Sin(x) + std::sqrt(y);\nPN_LOG_INFO("log(x)");\ncore::log::Start(d);\n',
                        "src/gfx/b.cpp": "f32 b = std::sin(x);\n"}):
            self.assertEqual(project_n.platform_maths(["src/core/a.cpp", "src/gfx/b.cpp"]), [])


class StandardIncludeTest(unittest.TestCase):
    def test_a_standard_include_in_a_source_is_reported(self):
        with temp_repo({"src/game.cpp": '#include "core/log.hpp"\n#include <vector>\n'}):
            self.assertEqual(project_n.standard_includes_in_sources(["src/game.cpp"]), ["src/game.cpp:2: #include <vector>"])

    def test_headers_and_library_includes_pass(self):
        with temp_repo({"src/game.hpp": "#pragma once\n#include <vector>\n", "tests/unit/a.cpp": "#include <doctest/doctest.h>\n"}):
            self.assertEqual(project_n.standard_includes_in_sources(["src/game.hpp", "tests/unit/a.cpp"]), [])


class SizedTypesTest(unittest.TestCase):
    def unsized(self, code, path="src/game.cpp"):
        with temp_repo({path: code}):
            return project_n.unsized_types([path])

    def test_built_in_and_std_number_types_are_reported(self):
        self.assertEqual(self.unsized("int a = 0;\ni32 b = 0;\nunsigned long c = 0;\nfloat d = 0;\nstd::uint32_t e = 0;\n"),
                         ["src/game.cpp:1: int a = 0;", "src/game.cpp:3: unsigned long c = 0;", "src/game.cpp:4: float d = 0;",
                          "src/game.cpp:5: std::uint32_t e = 0;"])

    def test_a_comment_naming_a_type_passes(self):
        self.assertEqual(self.unsized("i32 a = 0; // not an int\n"), [])

    def test_a_string_naming_a_type_passes(self):
        self.assertEqual(self.unsized('PN_LOG_INFO("an int: {}", a);\n'), [])

    def test_main_keeps_its_standard_signature(self):
        self.assertEqual(self.unsized("int main(int argc, char** argv) {}\n"), [])

    def test_the_types_header_itself_passes(self):
        self.assertEqual(self.unsized("using i32 = std::int32_t;\n", "src/core/types.hpp"), [])


class FacadeTest(unittest.TestCase):
    def bypassed(self, code, path="src/game.cpp"):
        with temp_repo({path: code}):
            return project_n.bypassed_facades([path])

    def test_printing_is_reported(self):
        self.assertEqual(self.bypassed('void F() {\n    std::println("hi");\n}\n'), ['src/game.cpp:2: std::println("hi");'])

    def test_c_and_sdl_output_is_reported(self):
        self.assertEqual(self.bypassed('void F() {\n    printf("a");\n    SDL_Log("b");\n}\n'),
                         ['src/game.cpp:2: printf("a");', 'src/game.cpp:3: SDL_Log("b");'])

    def test_a_direct_library_include_is_reported(self):
        self.assertEqual(self.bypassed("#include <tracy/Tracy.hpp>\n"), ["src/game.cpp:1: #include <tracy/Tracy.hpp>"])

    def test_an_entt_include_outside_the_ecs_facade_is_reported(self):
        self.assertEqual(self.bypassed("#include <entt/entity/view.hpp>\n", "src/game/ai.cpp"),
                         ["src/game/ai.cpp:1: #include <entt/entity/view.hpp>"])

    def test_the_ecs_facade_and_its_source_include_entt(self):
        self.assertEqual(self.bypassed("#include <entt/entity/registry.hpp>\n", "src/core/ecs.hpp"), [])
        self.assertEqual(self.bypassed("#include <entt/entity/registry.hpp>\n", "src/core/ecs.cpp"), [])

    def test_enkits_is_included_only_by_the_jobs_facade_source(self):
        self.assertEqual(self.bypassed("#include <enkiTS/TaskScheduler.h>\n", "src/game/arena.cpp"),
                         ["src/game/arena.cpp:1: #include <enkiTS/TaskScheduler.h>"])
        self.assertEqual(self.bypassed("#include <enkiTS/TaskScheduler.h>\n", "src/core/jobs.cpp"), [])

    def test_a_facade_header_includes_its_library(self):
        self.assertEqual(self.bypassed("#include <quill/Logger.h>\n", "src/core/log.hpp"), [])

    def test_a_facade_source_includes_its_library(self):
        self.assertEqual(self.bypassed("#include <quill/Backend.h>\n", "src/core/log.cpp"), [])

    def test_a_facade_source_including_another_library_is_reported(self):
        self.assertEqual(self.bypassed("#include <tracy/Tracy.hpp>\n", "src/core/log.cpp"), ["src/core/log.cpp:1: #include <tracy/Tracy.hpp>"])

    def test_a_facade_source_printing_is_reported(self):
        self.assertEqual(self.bypassed('std::println("hi");\n', "src/core/log.cpp"), ['src/core/log.cpp:1: std::println("hi");'])

    def test_output_after_a_url_in_a_string_is_reported(self):
        self.assertEqual(self.bypassed('PN_LOG_INFO("see https://x"); std::println("y");\n'),
                         ['src/game.cpp:1: PN_LOG_INFO("see https://x"); std::println("y");'])

    def test_a_string_naming_an_output_function_passes(self):
        self.assertEqual(self.bypassed('PN_LOG_INFO("call puts(x) failed");\n'), [])

    def test_a_quoted_library_include_is_reported(self):
        self.assertEqual(self.bypassed('#include "quill/Logger.h"\n'), ['src/game.cpp:1: #include "quill/Logger.h"'])

    def test_a_comment_naming_an_output_function_passes(self):
        self.assertEqual(self.bypassed('PN_LOG_INFO("x"); // not std::println\n'), [])

    def test_tests_may_print(self):
        self.assertEqual(self.bypassed('void F() { std::println("hi"); }\n', "tests/unit/game_tests.cpp"), [])


class TidyVerdictTest(unittest.TestCase):
    QUILL = "/home/dev/.xmake/packages/q/quill/include/quill/core/InlinedVector.h"

    def test_a_finding_in_our_code_fails(self):
        out = f"{project_n.ROOT}/src/main.cpp:34:48: error: repeated branch body [bugprone-branch-clone]\n"
        self.assertEqual(project_n.tidy_verdict(out, 1), (True, 0))

    def test_a_listed_finding_in_library_code_is_ignored(self):
        out = (f"{self.QUILL}:132:7: error: Out of bound access [clang-analyzer-security.ArrayBound,-warnings-as-errors]\n"
               f"{project_n.ROOT}/src/main.cpp:22:9: note: Calling 'Run'\n")
        self.assertEqual(project_n.tidy_verdict(out, 1), (False, 1))

    def test_another_check_in_a_listed_file_fails(self):
        out = f"{self.QUILL}:140:3: error: Null dereference [clang-analyzer-core.NullDereference]\n"
        self.assertEqual(project_n.tidy_verdict(out, 1), (True, 0))

    def test_a_finding_in_a_toolchain_header_fails(self):
        out = f"{project_n.ROOT}/.pixi/envs/default/include/c++/v1/vector:10:1: error: Null dereference [clang-analyzer-core.NullDereference]\n"
        self.assertEqual(project_n.tidy_verdict(out, 1), (True, 0))

    def test_a_crash_after_a_listed_finding_fails(self):
        out = f"{self.QUILL}:132:7: error: Out of bound access [clang-analyzer-security.ArrayBound]\n"
        self.assertEqual(project_n.tidy_verdict(out, -11), (True, 1))

    def test_a_failure_without_findings_fails(self):
        self.assertEqual(project_n.tidy_verdict("", 1), (True, 0))


class ApiDocsTest(unittest.TestCase):
    DOXYFILE = (project_n.ROOT / "docs" / "Doxyfile").read_text()

    def lint_docs(self, header):
        with temp_repo({"docs/Doxyfile": self.DOXYFILE, "src/clock.hpp": header}):
            quiet = functools.partial(subprocess.run, capture_output=True)  # Doxygen's error is the expected result
            with mock.patch.object(project_n.subprocess, "run", quiet), contextlib.redirect_stdout(io.StringIO()):
                project_n.check_api_docs(["src/clock.hpp"])

    FILE_DOC = "#pragma once\n/// @file\n/// Clocks.\n\n"  # a blank line ends the file comment

    def test_a_header_without_a_file_comment_fails(self):
        with self.assertRaises(project_n.Fail):
            self.lint_docs("#pragma once\n/// Starts counting from zero again.\nvoid Reset();\n")

    def test_an_undocumented_public_member_fails(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.lint_docs(self.FILE_DOC + "/// A clock.\nclass Clock {\npublic:\n    void Reset();\n};\n")

    def test_an_undocumented_free_function_fails(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.lint_docs(self.FILE_DOC + "namespace game {\nint Add(int a, int b);\n}\n")

    def test_a_documented_header_passes(self):
        self.lint_docs(self.FILE_DOC + "/// A clock.\nclass Clock {\npublic:\n    /// Starts counting from zero again.\n"
                       "    void Reset();\n\nprivate:\n    int ticks_ = 0;\n};\n")


class CoverageTest(unittest.TestCase):
    def test_counts_from_earlier_runs_are_deleted(self):
        saved = project_n.ROOT
        with tempfile.TemporaryDirectory() as d:
            project_n.ROOT = Path(d)
            try:
                stale = project_n.ROOT / project_n.build_dir("linux-gcc", "coverage") / "obj" / "main.gcda"
                stale.parent.mkdir(parents=True)
                stale.write_bytes(b"counts")
                with mock.patch.multiple(project_n, needs_container=lambda *a: False, cmd_test=lambda args: None,
                                         run=lambda cmd, **kw: None):
                    project_n.cmd_coverage(project_n.argparse.Namespace(container=False))
                self.assertFalse(stale.exists())
            finally:
                project_n.ROOT = saved


class FuzzTest(unittest.TestCase):
    def test_a_container_run_passes_the_target_and_time_on(self):
        argvs = []
        with mock.patch.object(project_n, "reenter", lambda preset, argv: argvs.append(argv)), \
             mock.patch.object(project_n, "IN_CONTAINER", False):
            project_n.cmd_fuzz(project_n.argparse.Namespace(target="smoke", time=5, container=True))
        self.assertEqual(argvs[0], ["fuzz", "smoke", "--time", "5"])


class DocsTest(unittest.TestCase):
    def test_only_quoted_commands_count(self):
        line = "Reviews a project_n branch: run `pixi run project_n test`, `project_n release` or ./dev fmt."
        self.assertEqual(project_n.DOC_COMMAND_RE.findall(line), ["test", "release", "fmt"])

    def test_agent_docs_quote_only_real_commands(self):
        self.assertEqual(project_n.stale_doc_commands(), [])


class SuppressionTest(unittest.TestCase):
    def test_sources_have_no_unexplained_suppressions(self):
        self.assertEqual(project_n.unexplained_suppressions(project_n.source_files()), [])

    def test_nolint_needs_checks_and_a_reason(self):
        explained = project_n.nolint_explained
        self.assertTrue(explained("f(); // NOLINT(cppcoreguidelines-pro-type-vararg): C API takes varargs"))
        self.assertTrue(explained("// NOLINTEND(bugprone-macro-parentheses)"))
        self.assertFalse(explained("f(); // NOLINT"))
        self.assertFalse(explained("// NOLINTNEXTLINE(readability-magic-numbers)"))

    def test_a_bare_nolint_is_reported(self):
        with temp_repo({"src/a.cpp": "int x = 0; // NOLINT\n"}):
            self.assertEqual(len(project_n.unexplained_suppressions(["src/a.cpp"])), 1)

    def test_an_uncommented_pragma_is_reported(self):
        with temp_repo({"src/a.cpp": '#pragma clang diagnostic ignored "-Wshadow"\n'}):
            self.assertEqual(len(project_n.unexplained_suppressions(["src/a.cpp"])), 1)

    def test_a_pragma_explained_on_the_line_above_passes(self):
        with temp_repo({"src/a.cpp": '// the SDL header shadows a name we cannot rename\n#pragma clang diagnostic ignored "-Wshadow"\n'}):
            self.assertEqual(project_n.unexplained_suppressions(["src/a.cpp"]), [])

    def test_a_nested_clang_tidy_without_a_reason_is_reported(self):
        with temp_repo({"src/core/.clang-tidy": "Checks: '-readability-magic-numbers'\n"}):
            self.assertEqual(project_n.unexplained_suppressions([]),
                             ["src/core/.clang-tidy: explain in a # comment why these checks can't be met"])

    def test_recognises_diagnostic_pragmas(self):
        for line in ("#pragma clang diagnostic ignored \"-Wshadow\"", "#  pragma GCC diagnostic ignored \"-Wconversion\"",
                     "#pragma warning(disable: 4996)"):
            self.assertRegex(line, project_n.PRAGMA_RE)
        self.assertNotRegex("#pragma clang diagnostic push", project_n.PRAGMA_RE)


class SanitizerTest(unittest.TestCase):
    def test_each_sanitizer_gets_its_own_build_dir(self):
        self.assertEqual(project_n.build_dir("linux-gcc", "debug").name, "debug")
        self.assertEqual(project_n.build_dir("linux-gcc", "debug", "address").name, "debug-address")

    def test_unity_builds_get_their_own_build_dir(self):
        self.assertEqual(project_n.build_dir("linux-gcc", "debug", unity=True).name, "debug-unity")

    def test_a_container_unity_run_passes_the_flag_on(self):
        argvs = []
        with mock.patch.object(project_n, "reenter", lambda preset, argv: argvs.append(argv)), \
             mock.patch.object(project_n, "IN_CONTAINER", False):
            project_n.cmd_test(project_n.argparse.Namespace(preset="linux-gcc", mode="debug", sanitizer="none", container=True,
                                                            target=None, test=None, args=[], unity=True))
        self.assertIn("--unity", argvs[0])

    def test_a_test_name_filters_by_file_or_else_by_case_name(self):
        self.assertEqual(project_n.test_filter("data_watch", "app_tests"),
                         "--source-file=*/data_watch_tests.cpp,*\\data_watch_tests.cpp")
        self.assertEqual(project_n.test_filter("log", "core_tests"),  # not logic_tests.cpp
                         "--source-file=*/log_tests.cpp,*\\log_tests.cpp")
        self.assertEqual(project_n.test_filter("game", "app_tests"),  # not all of unit/game/
                         "--source-file=*/game_tests.cpp,*\\game_tests.cpp")
        self.assertEqual(project_n.test_filter("no such file", "app_tests"), "--test-case=*no such file*")

    def test_a_test_file_of_another_suite_is_refused(self):  # doctest would pass the empty run
        with self.assertRaises(project_n.Fail):
            project_n.test_filter("data_watch", "core_tests")

    def test_names_group_test_files_by_suite(self):
        suites = project_n.test_names()
        self.assertIn("data_watch", suites["app_tests"])
        self.assertNotIn("archive", suites["app_tests"])
        self.assertEqual(suites["log_early_tests"], ["early"])  # a file list, not a glob; main.cpp isn't a test file

    @staticmethod
    def list_tests(target):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            project_n.cmd_test(project_n.argparse.Namespace(list_tests=True, target=target))
        return out.getvalue().split()

    def test_list_tests_with_a_suite_prints_its_test_files(self):
        self.assertEqual(self.list_tests("log_early_tests"), ["early"])

    def test_list_tests_without_a_suite_prints_every_suites_test_files(self):
        names = self.list_tests(None)
        self.assertLessEqual({"data_watch", "early", "log"}, set(names))

    def test_a_test_without_a_suite_is_refused(self):
        with self.assertRaises(project_n.Fail):
            project_n.cmd_test(project_n.argparse.Namespace(preset="linux-gcc", mode="debug", sanitizer="none", container=False,
                                                            target=None, test="data_watch", args=[], unity=False))

    def test_piped_help_opens_each_options_help_with_its_choices_once(self):  # shell completers read the first line
        out = io.StringIO()
        with contextlib.redirect_stdout(out), self.assertRaises(SystemExit):
            project_n.make_parser().parse_args(["test", "--help"])
        lines = out.getvalue().splitlines()
        for option in ("--preset", "--suite"):
            i = next(i for i, line in enumerate(lines) if line.lstrip().startswith(option))
            # argparse starts the help on the option's line, after two spaces, or on the next when the option is long
            help_line = lines[i].strip().split("  ", 1)[1] if "  " in lines[i].strip() else lines[i + 1].strip()
            self.assertTrue(help_line.startswith("[possible values: "), help_line)
            self.assertEqual(help_line.count("[possible values: "), 1, help_line)

    def test_suite_and_test_select_one_target_and_filter(self):
        args = project_n.make_parser().parse_args(["test", "--suite", "app_tests", "--test", "data_watch"])
        self.assertEqual((args.target, args.test), ("app_tests", "data_watch"))

    def test_support_matrix_is_enforced(self):
        project_n.check_sanitizer("linux-clang", "thread")
        project_n.check_sanitizer("windows-msvc", "none")
        with self.assertRaises(project_n.Fail):
            project_n.check_sanitizer("windows-msvc", "undefined")

    def test_ubsan_findings_fail_the_run(self):
        self.assertIn("halt_on_error=1", project_n.xmake_env(project_n.build_dir("linux-gcc", "debug"))["UBSAN_OPTIONS"])

    def test_users_own_options_win(self):
        with mock.patch.dict(project_n.os.environ, {"ASAN_OPTIONS": "detect_leaks=0"}):
            options = project_n.xmake_env(project_n.build_dir("linux-gcc", "debug"))["ASAN_OPTIONS"]
        self.assertTrue(options.startswith("abort_on_error=0") and options.endswith(":detect_leaks=0"), options)


class TestFilesTest(unittest.TestCase):
    @staticmethod
    def matches(path, stem):
        """Whether doctest's `--source-file` takes PATH: any comma-separated part matching, case-insensitively.
        fnmatchcase, as fnmatch's normcase turns `/` into `\\` on Windows and so would hide a separator mismatch."""
        return any(fnmatch.fnmatchcase(path.lower(), part.lower()) for part in project_n.test_file_pattern(stem).split(","))

    def test_a_file_pattern_matches_only_its_file(self):
        for stem, own, other in (("log", ("core", "log"), ("platform", "sdl_log")),
                                 ("clock", ("platform", "clock"), ("core", "tick_clock"))):
            spellings = {}
            for area, name in (own, other):
                spellings[name] = (f"/src/tests/unit/{area}/{name}_tests.cpp", f"z:\\src\\tests\\unit\\{area}\\{name}_tests.cpp")
            for path in spellings[own[1]]:
                self.assertTrue(self.matches(path, stem), (stem, path))
            for path in spellings[other[1]]:
                self.assertFalse(self.matches(path, stem), (stem, path))

    def test_no_suite_has_two_files_one_pattern_catches(self):
        for suite, stems in project_n.test_names().items():
            self.assertEqual(len(set(stems)), len(stems), f"{suite}: two test files share a name")
            for stem in stems:
                for other in stems:
                    if other != stem:
                        path = f"tests/unit/{suite}/{other}_tests.cpp"
                        self.assertFalse(self.matches(path, stem), f"{suite}: {stem}'s pattern takes {other}_tests.cpp")

    def test_the_lua_copy_of_the_pattern_builds_the_same_string(self):  # tests/xmake.lua's test_file_pattern
        lua = (project_n.ROOT / "tests" / "xmake.lua").read_text()
        expression = re.search(r"local function test_file_pattern\(stem\)\n    return (.*)\nend", lua).group(1)
        # A Lua concatenation of string literals and `stem`: evaluate it for one stem.
        parts = re.findall(r'"((?:[^"\\]|\\.)*)"|(stem)', expression)
        built = "".join("log" if name else literal.replace("\\\\", "\\") for literal, name in parts)
        self.assertEqual(expression.count(".."), len(parts) - 1, expression)  # nothing but literals, `stem` and `..`
        self.assertEqual(built, project_n.test_file_pattern("log"))

    def test_no_test_file_takes_the_name_of_the_rest(self):  # tests/xmake.lua registers `rest` beside the files' tests
        for suite, stems in project_n.test_names().items():
            self.assertNotIn("rest", stems, suite)

    @staticmethod
    def ran(target, test=None, args=()):
        ran = []
        with mock.patch.multiple(project_n, build=lambda *a: Path("build"), xmake_env=lambda bdir: {},
                                 needs_container=lambda *a: False, run=lambda argv, env: ran.append(argv)):
            project_n.cmd_test(project_n.argparse.Namespace(preset=project_n.NATIVE_PRESET, mode="debug", sanitizer="none",
                                                            container=False, target=target, test=test, args=list(args),
                                                            unity=False))
        return ran

    def test_a_suite_without_filters_runs_its_file_tests(self):
        self.assertEqual(self.ran("game_tests"), [["xmake", "test", "--output-on-failure", "game_tests/*"]])

    def test_a_suite_with_filters_runs_its_binary(self):
        self.assertEqual(self.ran("game_tests", test="save"),
                         [["xmake", "run", "game_tests", project_n.test_filter("save", "game_tests")]])
        self.assertEqual(self.ran("game_tests", args=["--", "--count"]), [["xmake", "run", "game_tests", "--count"]])

    def test_a_target_that_is_no_suite_runs_its_binary(self):  # the benchmark registers no test
        self.assertEqual(self.ran("headless_bench"), [["xmake", "run", "headless_bench"]])


class HelpersTest(unittest.TestCase):
    def test_with_version_replaces_only_the_version(self):
        lua = 'set_project("project_n")\nset_version("0.1.0")\nset_xmakever("3.1.2")\n'
        self.assertEqual(project_n.with_version(lua, "0.2.0"), lua.replace("0.1.0", "0.2.0"))

    def test_image_tags_are_stable_and_distinct(self):
        self.assertEqual(project_n.image_tag("linux"), project_n.image_tag("linux"))
        self.assertNotEqual(project_n.image_tag("linux"), project_n.image_tag("msvc"))
        self.assertRegex(project_n.image_tag("msvc"), r"^localhost/project_n-msvc:[0-9a-f]{12}$")

    def test_pushed_images_hold_no_msvc(self):
        # MSVC's licence forbids redistribution: it's installed into msvc_dir(), never into an image.
        for name, spec in project_n.IMAGES.items():
            if spec["push"]:
                self.assertNotIn("vsdownload.py", (project_n.ROOT / spec["file"]).read_text(), name)

    def test_manifest_reads_msvc_wine_versions(self):
        env_sh = 'BASE=z:x\nMSVCVER=14.50.35717\nSDKVER=10.0.26100.0\nMSVCDIR="$MSVCBASE\\\\tools"\n'
        self.assertEqual(project_n.msvc_versions(env_sh), {"msvc": "14.50.35717", "windows_sdk": "10.0.26100.0"})

    def test_macos_bundle_layout(self):
        signed = []
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "lib").mkdir()
            (root / "lib" / "libc++.1.dylib").write_bytes(b"dylib")
            (root / "project_n").write_bytes(b"binary")
            with (mock.patch.dict(project_n.os.environ, {"CONDA_PREFIX": d, "MACOSX_DEPLOYMENT_TARGET": "13.0"}),
                  mock.patch.object(project_n, "run", lambda cmd, **_: signed.append(Path(cmd[-1]).name))):
                project_n.macos_bundle(root / "dist", root / "project_n")
            contents = root / "dist" / "project_n.app" / "Contents"
            self.assertEqual((contents / "MacOS" / "project_n").read_bytes(), b"binary")
            self.assertTrue((contents / "Frameworks" / "libc++.1.dylib").is_file())
            plist = project_n.plistlib.loads((contents / "Info.plist").read_bytes())
        self.assertEqual((plist["CFBundleExecutable"], plist["LSMinimumSystemVersion"]), ("project_n", "13.0"))
        self.assertEqual(signed, ["libc++.1.dylib", "project_n.app", "project_n.app"])  # nested code first

    def test_msvc_install_follows_only_its_containerfile(self):
        self.assertRegex(project_n.msvc_dir().name, r"^msvc-[0-9a-f]{12}$")

    def test_container_command_mounts_repo_and_cache(self):
        with mock.patch.dict(project_n.os.environ):
            project_n.os.environ.pop("PROJECT_N_XMAKE_CACHE", None)  # CI sets it
            cmd = project_n.container_cmd("img", ["pixi", "run", "project_n", "build"])
        self.assertIn(f"{project_n.ROOT}:/src:Z", cmd)
        self.assertIn(f"{project_n.CACHE_VOLUME}:/home/dev/.xmake", cmd)
        self.assertEqual(cmd[-5:], ["img", "pixi", "run", "project_n", "build"])


class ShaderTest(unittest.TestCase):
    def resource(self, name, space, binding, readonly=True):
        return {"name": name, "set": space, "binding": binding, "readonly": readonly}

    def test_resources_in_sdl_order_are_counted(self):
        reflection = {"textures": [self.resource("atlas", 2, 0)], "images": [self.resource("noise", 2, 1)],
                      "ssbos": [self.resource("lights", 2, 2)], "ubos": [self.resource("post", 3, 0)]}
        self.assertEqual(project_n.shader_resources(reflection, 2, "x"), {"textures": 1, "images": 1, "ssbos": 1, "ubos": 1})

    def test_a_storage_buffer_before_a_texture_is_rejected(self):
        reflection = {"textures": [self.resource("atlas", 0, 1)], "ssbos": [self.resource("instances", 0, 0)]}
        with self.assertRaisesRegex(project_n.Fail, "SDL_GPU expects"):
            project_n.shader_resources(reflection, 0, "x")

    def test_a_uniform_buffer_in_the_resource_space_is_rejected(self):
        with self.assertRaisesRegex(project_n.Fail, "SDL_GPU expects"):
            project_n.shader_resources({"ubos": [self.resource("camera", 0, 0)]}, 0, "x")

    def test_separate_samplers_are_rejected(self):
        with self.assertRaisesRegex(project_n.Fail, "combined image samplers"):
            project_n.shader_resources({"separate_samplers": [self.resource("s", 2, 0)]}, 2, "x")

    def test_read_write_storage_is_rejected(self):
        with self.assertRaisesRegex(project_n.Fail, "read-write"):
            project_n.shader_resources({"ssbos": [self.resource("out", 0, 0, readonly=False)]}, 0, "x")

    def test_metal_numbers_storage_buffers_after_uniform_buffers(self):
        reflection = {"textures": [self.resource("atlas", 0, 0)], "ssbos": [self.resource("instances", 0, 1)],
                      "ubos": [self.resource("camera", 1, 0), self.resource("clock", 1, 1)]}
        options = project_n.msl_bindings(reflection, 0)
        mapped = [tuple(options[i + 1:i + 5]) for i in range(0, len(options), 5)]
        self.assertEqual(mapped, [("t0", "0", "0", "4"), ("s0", "0", "0", "4"), ("b0", "1", "0", "5"),
                                  ("b1", "1", "1", "5"), ("t1", "0", "2", "6")])

    def test_binaries_are_stale_until_the_manifest_matches(self):
        with temp_repo({"shaders/a.vert.hlsl": "x", "shaders/bin/a.vert.spv": "y"}) as root:
            self.assertTrue(project_n.stale_shaders())
            (root / project_n.SHADER_MANIFEST).write_text(project_n.shader_hashes())
            self.assertFalse(project_n.stale_shaders())
            (root / "shaders" / "a.vert.hlsl").write_text("changed")
            self.assertTrue(project_n.stale_shaders())

    def test_binaries_are_stale_after_a_shader_tool_bump(self):
        with temp_repo({"pixi.toml": 'spirv-cross = "==1.4.363.0"\n', "shaders/a.vert.hlsl": "x"}) as root:
            (root / project_n.SHADER_MANIFEST).parent.mkdir(parents=True, exist_ok=True)
            (root / project_n.SHADER_MANIFEST).write_text(project_n.shader_hashes())
            self.assertFalse(project_n.stale_shaders())
            (root / "pixi.toml").write_text('spirv-cross = "==1.4.364.0"\n')
            self.assertTrue(project_n.stale_shaders())

    def test_a_tool_bumped_for_one_platform_only_is_reported_and_stale(self):
        both = '[target.win-64.dependencies]\nspirv-cross = "==1.4.363.0"\n[target.linux-64.dependencies]\nspirv-cross = "==1.4.363.0"\n'
        with temp_repo({"pixi.toml": both, "shaders/a.vert.hlsl": "x"}) as root:
            (root / project_n.SHADER_MANIFEST).parent.mkdir(parents=True, exist_ok=True)
            (root / project_n.SHADER_MANIFEST).write_text(project_n.shader_hashes())
            self.assertEqual(project_n.unequal_shader_pins(), [])
            (root / "pixi.toml").write_text(both.replace('[target.linux-64.dependencies]\nspirv-cross = "==1.4.363.0"',
                                                          '[target.linux-64.dependencies]\nspirv-cross = "==1.4.999.0"'))
            self.assertEqual(project_n.unequal_shader_pins(), ["spirv-cross"])
            self.assertTrue(project_n.stale_shaders())

    def test_a_shader_name_that_isnt_an_identifier_fails_before_any_output_is_removed(self):
        with temp_repo({"shaders/bad-name.vert.hlsl": "x", "shaders/bin/a.vert.spv": "y"}) as root:
            with mock.patch.multiple(project_n, HOST="linux", unequal_shader_pins=list), \
                    self.assertRaisesRegex(project_n.Fail, "bad-name.vert.hlsl: name shaders"):
                project_n.cmd_shaders(mock.Mock())
            self.assertTrue((root / "shaders" / "bin" / "a.vert.spv").is_file())

    def test_text_outputs_get_lf_endings(self):
        with tempfile.TemporaryDirectory() as d:
            msl = Path(d, "a.vert.msl")
            msl.write_bytes(b"one\r\ntwo\r\n")
            project_n.lf_endings(msl)
            self.assertEqual(msl.read_bytes(), b"one\ntwo\n")


def png_header(width, height):
    return b"\x89PNG\r\n\x1a\n" + (13).to_bytes(4, "big") + b"IHDR" + width.to_bytes(4, "big") + height.to_bytes(4, "big")


class AtlasTest(unittest.TestCase):
    def test_sprites_keep_their_gutters_and_never_overlap(self):
        sizes = [(100, 40), (30, 60), (90, 50), (60, 60), (20, 10)]
        places = project_n.pack_atlas(sizes, page=200, gutter=8)
        rects = [(p, x, y, x + w, y + h) for (p, x, y), (w, h) in zip(places, sizes)]
        for p, x0, y0, x1, y1 in rects:
            self.assertTrue(x0 >= 8 and y0 >= 8 and x1 <= 192 and y1 <= 192, (x0, y0, x1, y1))
        for i, a in enumerate(rects):
            for b in rects[i + 1:]:
                apart = a[0] != b[0] or a[3] + 8 <= b[1] or b[3] + 8 <= a[1] or a[4] + 8 <= b[2] or b[4] + 8 <= a[2]
                self.assertTrue(apart, (a, b))

    def test_the_packer_keeps_a_32_texel_gutter(self):
        (_, x0, y0), (_, x1, y1) = project_n.pack_atlas([(10, 40), (24, 48)])
        self.assertTrue(min(x0, y0, x1, y1) >= 32, (x0, y0, x1, y1))
        first, second = sorted([(x0, 10), (x1, 24)])  # the packer places the taller first
        self.assertTrue(second[0] >= first[0] + first[1] + 32, (x0, x1))

    def test_the_manifest_states_its_gutter(self):
        text = project_n.atlas_manifest([("a", 10, 40, 0, 0)], [(0, 32, 32)], [])
        self.assertIn("\ngutter: 32\n", text)

    def test_a_full_page_opens_the_next(self):
        self.assertEqual([p for p, _, _ in project_n.pack_atlas([(80, 80)] * 3, page=100, gutter=4)], [0, 1, 2])

    def test_a_sprite_too_large_for_a_page_fails(self):
        with self.assertRaisesRegex(project_n.Fail, "larger than"):
            project_n.pack_atlas([(95, 10)], page=100, gutter=4)

    def test_the_list_and_the_images_must_agree(self):
        with temp_repo({"art/sprites.toml": "[a]\n[b]\n"}) as root:
            (root / "assets" / "sprites").mkdir(parents=True)
            (root / "assets" / "sprites" / "a.png").write_bytes(png_header(10, 20))
            with self.assertRaisesRegex(project_n.Fail, r"only in the list: \['b'\]"):
                project_n.read_sprites()
            (root / "assets" / "sprites" / "b.png").write_bytes(png_header(4, 4))
            self.assertEqual(project_n.read_sprites(), [("a", 10, 20, 5, 20), ("b", 4, 4, 2, 4)])

    def test_a_foot_outside_its_image_fails(self):
        with temp_repo({"art/sprites.toml": "[a]\nfoot = [11, 0]\n"}) as root:
            (root / "assets" / "sprites").mkdir(parents=True)
            (root / "assets" / "sprites" / "a.png").write_bytes(png_header(10, 20))
            with self.assertRaisesRegex(project_n.Fail, "outside"):
                project_n.read_sprites()

    def sprites(self, listing, images=("a",)):
        root = self.enterContext(temp_repo({"art/sprites.toml": listing}))
        (root / "assets" / "sprites").mkdir(parents=True)
        for name in images:
            (root / "assets" / "sprites" / f"{name}.png").write_bytes(png_header(10, 20))
        return root

    def test_a_foot_that_isnt_two_integers_fails(self):
        for foot in ("[1.5, 2]", "[true, false]", "[1]", "[1, 2, 3]", '"x"'):
            with self.subTest(foot=foot):
                self.sprites(f"[a]\nfoot = {foot}\n")
                with self.assertRaisesRegex(project_n.Fail, "a's foot"):
                    project_n.read_sprites()

    def test_a_sprite_that_isnt_a_table_fails(self):
        self.sprites("a = 1\n")
        with self.assertRaisesRegex(project_n.Fail, "a must be a table"):
            project_n.read_sprites()

    def test_names_that_make_one_enum_member_fail(self):
        self.sprites("[foo]\n[foo_]\n", images=("foo", "foo_"))
        with self.assertRaisesRegex(project_n.Fail, "foo_ and foo are both SpriteId::Foo"):
            project_n.read_sprites()

    def test_an_empty_sprite_list_fails(self):
        self.sprites("", images=())
        with self.assertRaisesRegex(project_n.Fail, "lists no sprites"):
            project_n.read_sprites()

    def test_the_atlas_is_stale_until_the_hashes_match(self):
        with temp_repo({"art/sprites.toml": "[a]\n", "art/scenes.toml": '[one]\nsprites = ["a"]\n'}) as root:
            (root / "assets" / "sprites").mkdir(parents=True)
            (root / "assets" / "sprites" / "a.png").write_bytes(png_header(10, 20))
            self.assertTrue(project_n.stale_atlas())
            with contextlib.redirect_stdout(io.StringIO()):
                project_n.cmd_atlas(mock.Mock(placeholders=False))
            self.assertFalse(project_n.stale_atlas())
            manifest = (root / "assets" / "manifest.yaml").read_text(encoding="utf-8")
            self.assertIn("  - { name: a, file: sprites/a.png, page: 0, region: [32, 32, 10, 20], foot: [5, 20] }", manifest)
            self.assertIn("  - { name: one, sprites: [a] }", manifest)
            self.assertIn('    "a",', (root / "src" / "core" / "generated" / "atlas.hpp").read_text(encoding="utf-8"))
            (root / "assets" / "manifest.yaml").write_text(manifest + "# edited\n", encoding="utf-8")
            self.assertTrue(project_n.stale_atlas())
            with contextlib.redirect_stdout(io.StringIO()):
                project_n.cmd_atlas(mock.Mock(placeholders=False))
            (root / "art" / "scenes.toml").write_text('[two]\nsprites = ["a"]\n', encoding="utf-8")
            self.assertTrue(project_n.stale_atlas())

    def test_scenes_must_list_known_sprites_once(self):
        sprites = [("a", 10, 20, 5, 20), ("b", 4, 4, 2, 4)]
        for listing, error in (("", "lists no scenes"), ("[one]\n", "one must list its sprites"),
                               ("[one]\nsprites = []\n", "one must list its sprites"),
                               ('[one]\nsprites = ["a", "c"]\n', r"one names sprites .* doesn't have: \['c'\]"),
                               ('[one]\nsprites = ["a", "a"]\n', "one names a sprite twice")):
            with (self.subTest(listing=listing), temp_repo({"art/scenes.toml": listing}),
                  self.assertRaisesRegex(project_n.Fail, error)):
                project_n.read_scenes(sprites)
        with temp_repo({"art/scenes.toml": '[one]\nsprites = ["b"]\n[two]\nsprites = ["a", "b"]\n'}):
            self.assertEqual(project_n.read_scenes(sprites), [("one", ["b"]), ("two", ["a", "b"])])


class GitLfsTest(unittest.TestCase):
    def test_a_clone_is_set_up_only_with_every_value(self):
        config = dict(project_n.CLONE_CONFIG)

        def capture(cmd, check=True):
            return config.get(cmd[-1], "") + "\n"
        with mock.patch.object(project_n, "capture", capture):
            self.assertTrue(project_n.clone_set_up())
            config["filter.lfs.process"] = "git-lfs filter-process"  # the host's git-lfs, not Pixi's
            self.assertFalse(project_n.clone_set_up())

    def test_setup_writes_local_config_and_fetches(self):
        ran = []
        with mock.patch.object(project_n, "run", ran.append):
            project_n.cmd_setup(argparse.Namespace())
        self.assertTrue(all(cmd[:3] == ["git", "config", "--local"] for cmd in ran[:-1]))
        self.assertEqual({cmd[3] for cmd in ran[:-1]}, set(project_n.CLONE_CONFIG))
        self.assertEqual(ran[-1], ["git", "lfs", "pull"])

    def test_missing_files_are_the_ones_marked_with_a_minus(self):
        listing = "4d7a2146b5 * assets/sprites/a - b.png\n9c1e00aa71 - assets/fonts/b otf.otf\n"
        with mock.patch.object(project_n, "capture", lambda cmd, check=True: listing), \
             mock.patch.object(project_n.shutil, "which", lambda name: "/env/bin/git-lfs"):
            self.assertEqual(project_n.lfs_missing(), ["assets/fonts/b otf.otf"])

    def test_missing_files_cant_be_told_without_git_lfs(self):
        with mock.patch.object(project_n.shutil, "which", lambda name: None):
            self.assertEqual(project_n.lfs_missing(), ["(git-lfs isn't in the environment)"])

    def test_the_cache_key_ignores_whether_objects_are_downloaded(self):
        listing = "faa6c9df65 - assets/fonts/a.otf\n"
        self.assertEqual(project_n.lfs_key(listing), project_n.lfs_key(listing.replace(" - ", " * ")))

    def test_pointers_are_found_and_content_is_not(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "fonts").mkdir()
            (Path(tmp) / "fonts" / "a.otf").write_bytes(project_n.LFS_POINTER + b"oid sha256:00\nsize 1\n")
            (Path(tmp) / "b.png").write_bytes(b"\x89PNG\r\n")
            self.assertEqual(project_n.lfs_pointers(tmp), ["fonts/a.otf"])

    def test_the_cache_key_follows_the_objects(self):
        one = "faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9 * assets/fonts/a.otf\n"
        other = one.replace("faa6", "0aa6")
        self.assertEqual(project_n.lfs_key(one), project_n.lfs_key(one))
        self.assertNotEqual(project_n.lfs_key(one), project_n.lfs_key(other))
        self.assertRegex(project_n.lfs_key(one), r"^lfs-[0-9a-f]{16}$")

    def test_prune_keeps_only_the_checkouts_objects(self):
        ran = []
        with mock.patch.object(project_n, "run", ran.append):
            project_n.cmd_lfs_prune(argparse.Namespace())
        self.assertEqual(ran[0][-2:], ["lfs", "prune"])
        for window in ("lfs.fetchrecentrefsdays=0", "lfs.fetchrecentcommitsdays=0", "lfs.pruneoffsetdays=0"):
            self.assertIn(window, ran[0])

    def test_setup_twice_changes_nothing(self):
        config = {}

        def run(cmd):
            if cmd[:3] == ["git", "config", "--local"]:
                config[cmd[3]] = cmd[4]
        with mock.patch.object(project_n, "run", run):
            project_n.cmd_setup(argparse.Namespace())
            first = dict(config)
            project_n.cmd_setup(argparse.Namespace())
        self.assertEqual(config, first)
        self.assertEqual(config, project_n.CLONE_CONFIG)

    def test_lint_checks_the_range_or_head(self):
        ran = []
        with mock.patch.object(project_n, "run", ran.append):
            project_n.check_lfs_pointers("main..HEAD")
            project_n.check_lfs_pointers(None)
        self.assertEqual(ran, [["git", "lfs", "fsck", "--pointers", "main..HEAD"], ["git", "lfs", "fsck", "--pointers"]])

    def test_a_plain_blob_fails_lint(self):
        def run(cmd):
            raise subprocess.CalledProcessError(1, cmd)
        with mock.patch.object(project_n, "run", run), self.assertRaises(project_n.Fail):
            project_n.check_lfs_pointers("main..HEAD")

    def test_artifact_refuses_pointers_before_anything_else(self):
        with mock.patch.multiple(project_n, tree_dirty=lambda: "", lfs_pointers=lambda folder: ["fonts/a.otf"],
                                 reenter=mock.Mock(side_effect=AssertionError("built anyway")),
                                 build=mock.Mock(side_effect=AssertionError("built anyway"))), \
             self.assertRaises(project_n.Fail):
            project_n.cmd_artifact(argparse.Namespace(preset="linux-gcc", reuse_build=False))


class XmakeProjectTest(unittest.TestCase):
    """XMake takes an ancestor folder holding a project over the current one, so a worktree inside the main checkout
    would build the main checkout's sources: every XMake task names this checkout as its project."""

    def test_task_names_this_checkout(self):
        self.assertEqual(project_n.resolve(["xmake", "build"])[1:], ["build", "-P", str(project_n.ROOT)])

    def test_project_comes_before_program_arguments(self):
        self.assertEqual(project_n.resolve(["xmake", "run", "game", "--quit-after", "5"])[1:],
                         ["run", "-P", str(project_n.ROOT), "game", "--quit-after", "5"])

    def test_global_options_untouched(self):
        self.assertEqual(project_n.resolve(["xmake", "--version"])[1:], ["--version"])

    def test_other_programs_untouched(self):
        self.assertEqual(project_n.resolve(["git", "status"])[1:], ["status"])


class PruneWorktreesTest(unittest.TestCase):
    MAIN = Path("/repo")
    PORCELAIN = ("worktree /repo\nHEAD 1111\nbranch refs/heads/main\n\n"
                 "worktree /repo/.worktrees/done\nHEAD 2222\nbranch refs/heads/done\n\n"
                 "worktree /repo/.claude/worktrees/busy\nHEAD 3333\nbranch refs/heads/worktree-busy\nlocked claude session\n\n"
                 "worktree /repo/.claude/worktrees/loose\nHEAD 4444\ndetached\n\n"
                 "worktree /tmp/elsewhere\nHEAD 5555\nbranch refs/heads/elsewhere\n")

    def test_owns_only_worktrees_under_the_project_folders(self):
        owned = project_n.owned_worktrees(self.PORCELAIN, self.MAIN)
        self.assertEqual([(w["path"], w["branch"], w["locked"], w["prunable"]) for w in owned],
                         [(Path("/repo/.worktrees/done"), "done", False, False),
                          (Path("/repo/.claude/worktrees/busy"), "worktree-busy", True, False),
                          (Path("/repo/.claude/worktrees/loose"), None, False, False)])

    def test_a_folder_deleted_by_hand_is_prunable(self):
        porcelain = "worktree /repo/.worktrees/gone\nHEAD 6666\nbranch refs/heads/gone\nprunable gitdir file points to non-existent location\n"
        self.assertTrue(project_n.owned_worktrees(porcelain, self.MAIN)[0]["prunable"])

    def test_skips_prunable_worktrees(self):
        gone = self.worktree(path=Path("/repo/.worktrees/gone"), prunable=True)

        def capture(cmd, check=True):
            self.assertNotIn("-C", cmd)  # its folder is gone: asking git about it would fail
            return self.PORCELAIN

        with mock.patch.object(project_n, "main_checkout", lambda: self.MAIN), mock.patch.object(project_n, "capture", capture), \
                mock.patch.object(project_n, "owned_worktrees", lambda porcelain, main: [gone]), \
                mock.patch.object(project_n, "run") as run, contextlib.redirect_stdout(io.StringIO()):
            project_n.prune_worktrees(argparse.Namespace())
        self.assertEqual(run.call_args_list[-1].args[0], ["git", "worktree", "prune"])
        self.assertFalse([c for c in run.call_args_list if "remove" in c.args[0]])

    def prune_gone(self, remote, cherry):
        """prune_worktrees over one worktree whose folder was deleted by hand: (run calls, printed text)."""
        gone = self.worktree(path=Path("/repo/.worktrees/gone"), branch="gone", prunable=True)
        answers = {"list": self.PORCELAIN, "ls-remote": remote, "cherry": cherry}
        fake = lambda cmd, check=True: next(v for k, v in answers.items() if k in cmd)
        with mock.patch.object(project_n, "main_checkout", lambda: self.MAIN), mock.patch.object(project_n, "capture", fake), \
                mock.patch.object(project_n, "owned_worktrees", lambda porcelain, main: [gone]), \
                mock.patch.object(project_n, "run") as run, contextlib.redirect_stdout(io.StringIO()) as out:
            project_n.prune_worktrees(argparse.Namespace())
        return [c.args[0] for c in run.call_args_list], out.getvalue()

    def test_a_deleted_folder_is_reported_and_its_merged_branch_deleted(self):
        ran, out = self.prune_gone(remote="", cherry="- 6666\n")
        self.assertIn("folder deleted", out)
        self.assertEqual(ran[-2:], [["git", "worktree", "prune"], ["git", "branch", "-D", "gone"]])

    def test_a_deleted_folder_keeps_an_unmerged_branch(self):
        ran, out = self.prune_gone(remote="", cherry="+ 6666\n")
        self.assertIn("not in origin/main", out)
        self.assertNotIn(["git", "branch", "-D", "gone"], ran)

    def test_keeps_this_checkout(self):
        self.assertIn("this checkout", self.reason(self.worktree(path=project_n.ROOT)))

    def test_locked_says_how_to_unlock(self):
        self.assertIn("git worktree unlock", self.reason(self.worktree(locked=True)))

    def worktree(self, **fields):
        return {"path": Path("/repo/.worktrees/done"), "branch": "done", "locked": False, "prunable": False, **fields}

    def reason(self, worktree, status="", remote="", cherry="- 2222\n"):
        answers = {"status": status, "ls-remote": remote, "cherry": cherry}
        fake = lambda cmd, check=True: next(v for k, v in answers.items() if k in cmd)
        with mock.patch.object(project_n, "capture", fake):
            return project_n.worktree_unremovable(worktree)

    def test_refuses_uncommitted_changes(self):
        self.assertIn("uncommitted", self.reason(self.worktree(), status=" M src/main.cpp\n"))

    def test_refuses_a_branch_still_on_origin(self):
        self.assertIn("still on origin", self.reason(self.worktree(), remote="2222\trefs/heads/done\n"))

    def test_refuses_commits_missing_from_main(self):
        self.assertIn("not in origin/main", self.reason(self.worktree(), cherry="- 2222\n+ 2223\n"))

    def test_refuses_a_locked_worktree(self):
        self.assertIn("locked", self.reason(self.worktree(locked=True)))

    def test_refuses_a_detached_worktree(self):
        self.assertIn("no branch", self.reason(self.worktree(branch=None)))

    def test_keeps_a_branch_with_no_commits_of_its_own(self):
        # Never pushed and nothing committed yet: a session may have just made it; its build/ and ledger aren't tracked.
        self.assertIn("no commits", self.reason(self.worktree(), cherry=""))

    def test_merged_worktree_is_removable(self):
        self.assertIsNone(self.reason(self.worktree()))

    def test_removes_merged_worktrees_without_force(self):
        done = self.worktree()
        with mock.patch.object(project_n, "main_checkout", lambda: self.MAIN), \
                mock.patch.object(project_n, "capture", lambda cmd, check=True: self.PORCELAIN), \
                mock.patch.object(project_n, "owned_worktrees", lambda porcelain, main: [done]), \
                mock.patch.object(project_n, "worktree_unremovable", lambda w: None), \
                mock.patch.object(project_n, "run") as run, contextlib.redirect_stdout(io.StringIO()):
            project_n.prune_worktrees(argparse.Namespace())
        ran = [c.args[0] for c in run.call_args_list]
        self.assertIn(["git", "worktree", "remove", Path("/repo/.worktrees/done")], ran)
        self.assertIn(["git", "branch", "-D", "done"], ran)
        self.assertFalse([c for c in ran if "--force" in c or "-f" in c])

    def test_keeps_unremovable_worktrees(self):
        with mock.patch.object(project_n, "main_checkout", lambda: self.MAIN), \
                mock.patch.object(project_n, "capture", lambda cmd, check=True: self.PORCELAIN), \
                mock.patch.object(project_n, "owned_worktrees", lambda porcelain, main: [self.worktree()]), \
                mock.patch.object(project_n, "worktree_unremovable", lambda w: "uncommitted changes"), \
                mock.patch.object(project_n, "run") as run, contextlib.redirect_stdout(io.StringIO()) as out:
            project_n.prune_worktrees(argparse.Namespace())
        self.assertFalse([c for c in run.call_args_list if "remove" in c.args[0]])
        self.assertIn("uncommitted changes", out.getvalue())


class ParallelTidyTest(unittest.TestCase):
    """lint runs one clang-tidy per unit across all cores and judges each unit as tidy_verdict judges one run."""

    QUILL = "/home/dev/.xmake/packages/q/quill/include/quill/core/InlinedVector.h"
    IGNORED = f"{QUILL}:132:7: error: Out of bound access [clang-analyzer-security.ArrayBound,-warnings-as-errors]\n"

    def tidy(self, results, fix=False):
        """tidy_units over the units in `results` ({unit: (output, returncode)}): (verdict, units run, units the serial
        fix pass got or None, what was printed)."""
        ran, fixed = [], []

        def one(bdir, unit, cache=None):
            ran.append(unit)
            return results[unit]

        with mock.patch.object(project_n, "tidy_one", one), \
                mock.patch.object(project_n, "tidy_fix", lambda bdir, units: fixed.append(units)), \
                contextlib.redirect_stdout(io.StringIO()) as out:
            verdict = project_n.tidy_units(Path("build/linux-clang/debug"), list(results), fix)
        return verdict, ran, fixed, out.getvalue()

    def test_clean_units_pass(self):
        verdict, _, _, _ = self.tidy({"src/a.cpp": ("", 0), "src/b.cpp": ("", 0)})
        self.assertEqual(verdict, (False, 0))

    def test_crash_in_one_file_fails_beside_ignored_findings(self):
        verdict, _, _, _ = self.tidy({"src/a.cpp": (self.IGNORED, 1), "src/b.cpp": (self.IGNORED, -11)})
        self.assertTrue(verdict[0])

    def test_error_without_a_finding_fails_beside_ignored_findings(self):
        # "Error while processing" with no finding: today's single run fails it; one unit's ignored finding mustn't hide it.
        verdict, _, _, _ = self.tidy({"src/a.cpp": (self.IGNORED, 1), "src/b.cpp": ("Error while processing src/b.cpp.\n", 1)})
        self.assertTrue(verdict[0])

    def test_compiler_error_fails(self):
        out = f"{project_n.ROOT}/src/a.cpp:3:10: error: 'x.hpp' file not found [clang-diagnostic-error]\n"
        verdict, _, _, _ = self.tidy({"src/a.cpp": (out, 1)})
        self.assertTrue(verdict[0])

    def test_ignored_count_counts_each_finding_once(self):
        # The same library finding reached through two units, and spelled two ways, is one ignored finding.
        other = self.IGNORED.replace(":132:7:", ":156:14:")
        verdict, _, _, _ = self.tidy({"src/a.cpp": (self.IGNORED, 1), "src/b.cpp": (self.IGNORED + other, 1),
                                      "src/c.cpp": (self.IGNORED.replace(self.QUILL, self.QUILL.replace("/q/", "/./q/")), 1)})
        self.assertEqual(verdict, (False, 2))

    def test_tidy_one_keeps_errors_with_findings(self):
        with mock.patch.object(project_n.subprocess, "run",
                               return_value=subprocess.CompletedProcess([], 1, stdout="out and err\n")) as sub:
            self.assertEqual(project_n.tidy_one(Path("build/x"), "src/a.cpp"), ("out and err\n", 1))
        self.assertEqual(sub.call_args.kwargs["stderr"], subprocess.STDOUT)

    def test_jobs_follow_the_cpus_this_process_may_use(self):
        with mock.patch.object(project_n.os, "process_cpu_count", return_value=3):
            self.assertEqual(project_n.jobs(), 3)
        with mock.patch.object(project_n.os, "process_cpu_count", return_value=None):
            self.assertEqual(project_n.jobs(), 1)

    def test_every_unit_checked_once(self):
        units = {f"src/{n}.cpp": ("", 0) for n in "abcdef"}
        _, ran, _, _ = self.tidy(units)
        self.assertEqual(sorted(ran), sorted(units))

    FINDING = "{root}/src/core/types.hpp:37:1: error: use 'using' instead of 'typedef' [modernize-use-using]\n   37 | typedef int X;\n"

    def test_fix_reruns_only_failing_units_in_one_serial_pass(self):
        # clang-tidy --fix, as before the pool: exported fixes plus clang-apply-replacements would also apply note
        # fix-its that only silence a warning; one serial process never writes a shared header twice.
        failing = self.FINDING.format(root=project_n.ROOT)
        _, _, fixed, _ = self.tidy({"src/a.cpp": (failing, 1), "src/b.cpp": ("", 0), "src/c.cpp": (failing, 1)}, fix=True)
        self.assertEqual(fixed, [["src/a.cpp", "src/c.cpp"]])

    def test_no_fix_pass_when_nothing_failed(self):
        _, _, fixed, _ = self.tidy({"src/a.cpp": (self.IGNORED, 1), "src/b.cpp": ("", 0)}, fix=True)
        self.assertEqual(fixed, [])

    def test_no_fix_pass_without_fix(self):
        _, _, fixed, _ = self.tidy({"src/a.cpp": (self.FINDING.format(root=project_n.ROOT), 1)})
        self.assertEqual(fixed, [])

    def test_a_parallel_unit_never_fixes(self):
        cmd = project_n.tidy_command(Path("build/x"), "src/a.cpp")
        self.assertFalse([a for a in cmd if str(a).startswith(("--fix", "--export-fixes"))])
        self.assertEqual(cmd[-1], "src/a.cpp")

    def test_an_ignored_finding_is_counted_not_printed(self):
        # The listed Quill finding, with its notes and code lines, reached through every PN_LOG_* call: counted, not shown.
        quill = (self.IGNORED + f"  132 |       return _data[index];\n      |       ^\n"
                 f"{project_n.ROOT}/src/main.cpp:22:9: note: Calling 'Run'\n   22 |         Run();\n      |         ^\n")
        verdict, _, _, out = self.tidy({"src/a.cpp": (quill, 1), "src/b.cpp": (quill, 1)})
        self.assertEqual(verdict, (False, 1))
        self.assertEqual(out, "")
        finding = self.FINDING.format(root=project_n.ROOT)
        verdict, _, _, out = self.tidy({"src/a.cpp": (quill + finding, 1)})
        self.assertEqual(verdict, (True, 1))
        self.assertEqual(out, finding)

    def test_a_shared_header_finding_prints_once(self):
        finding = self.FINDING.format(root=project_n.ROOT)
        _, _, _, out = self.tidy({"src/a.cpp": (finding, 1), "src/b.cpp": (finding, 1)})
        self.assertEqual(out.count("modernize-use-using"), 1)

    def test_a_finding_prints_once_however_its_path_is_spelled(self):
        # clang-tidy names a shared header by the include path a unit reached it through.
        def finding(path):  # a note naming the same header, as the widening check's "make conversion explicit" does
            return (f"{path}:39:49: error: implicit widening [bugprone-implicit-widening-of-multiplication-result]\n"
                    f"{path}:39:49: note: make conversion explicit to silence this warning\n")
        spellings = ["./src/core/types.hpp", "src/core/types.hpp", f"{project_n.ROOT}/src/core/types.hpp"]
        _, _, _, out = self.tidy({f"src/{n}.cpp": (finding(path), 1) for n, path in zip("abc", spellings)})
        self.assertEqual(out.count("implicit-widening"), 1)

    def test_distinct_findings_all_print(self):
        a = f"{project_n.ROOT}/src/a.cpp:1:1: error: one [bugprone-x]\n"
        b = f"{project_n.ROOT}/src/b.cpp:2:2: error: two [bugprone-y]\n"
        _, _, _, out = self.tidy({"src/a.cpp": (a, 1), "src/b.cpp": (b, 1)})
        self.assertIn("bugprone-x", out)
        self.assertIn("bugprone-y", out)

    def test_text_outside_findings_always_prints(self):
        _, _, _, out = self.tidy({"src/a.cpp": ("Error while processing src/a.cpp.\n", 1),
                                  "src/b.cpp": ("Error while processing src/b.cpp.\n", 1)})
        self.assertIn("src/a.cpp.", out)
        self.assertIn("src/b.cpp.", out)


class TidyCacheTest(unittest.TestCase):
    """Each unit's clang-tidy result is kept under a key of everything it depends on, read on branches only; main and
    tags lint cold, and a tag stores nothing (spec 2026-11-13 §2b)."""

    BDIR = Path("build/x")
    UNIT = "src/a.cpp"
    FINDING = "src/a.cpp:1:5: error: variable 'x' is non-const [cppcoreguidelines-avoid-non-const-global-variables]\n"

    def setUp(self):
        self.root = self.enterContext(temp_repo({".clang-tidy": "Checks: '*'\n", self.UNIT: "int x;\n"}))
        self.cache = self.root / "tidy-cache"
        self.text, self.version = b'# 1 "src/a.cpp"\nint x;\n', "LLVM version 22.1.0\n"
        self.output, self.ran = (self.FINDING, 1), []
        self.compdb(["-O0"])
        self.enterContext(self.ci())
        self.enterContext(mock.patch.multiple(project_n, preprocessed=lambda entry: self.text,
                                              tidy_version=lambda: self.version))
        real = subprocess.run

        def fake(cmd, *args, **kwargs):  # clang-tidy itself; everything else (git) really runs
            if "--quiet" not in cmd:
                return real(cmd, *args, **kwargs)
            self.ran.append(cmd[-1])
            return subprocess.CompletedProcess(cmd, self.output[1], stdout=self.output[0])

        self.enterContext(mock.patch.object(project_n.subprocess, "run", fake))

    def ci(self, ref=None, cache=None):
        """os.environ with CI's ref and object cache folder, or neither (a local run)."""
        env = {k: v for k, v in os.environ.items() if k not in (project_n.BUILD_CACHE_ENV, "GITHUB_REF")}
        env |= {"GITHUB_REF": ref} if ref else {}
        env |= {project_n.BUILD_CACHE_ENV: str(cache)} if cache else {}
        return mock.patch.dict(project_n.os.environ, env, clear=True)

    def compdb(self, *flag_sets, out="build/x/.objs/a.cpp.o"):
        """One compile database entry for UNIT per set of flags (two targets may compile one file)."""
        entries = [{"directory": str(self.root), "file": self.UNIT,
                    "arguments": ["/usr/bin/clang++", "-c", "-std=c++23", *flags, "-o", out, self.UNIT]}
                   for flags in flag_sets]
        path = self.root / self.BDIR / "compile_commands.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(entries), encoding="utf-8")

    def lint(self, cache=None):
        return project_n.tidy_one(self.BDIR, self.UNIT, cache or self.cache)

    def test_a_cached_unit_is_not_linted_again(self):
        first = self.lint()
        self.assertEqual(self.lint(), first)
        self.assertEqual(first, self.output)
        self.assertEqual(self.ran, [self.UNIT])
        self.assertEqual(len(list(self.cache.glob("*.json"))), 1)

    def test_each_key_input_misses(self):
        key = project_n.tidy_key(self.BDIR, self.UNIT)
        self.assertEqual(project_n.tidy_key(self.BDIR, self.UNIT), key)
        self.compdb(["-O0"], out="build/y/a.o")  # the output path is no input
        self.assertEqual(project_n.tidy_key(self.BDIR, self.UNIT), key)
        moved = self.root / "build/moved/compile_commands.json"  # nor where the build folder is (clang-tidy's -p)
        moved.parent.mkdir(parents=True)
        moved.write_bytes((self.root / self.BDIR / "compile_commands.json").read_bytes())
        self.assertEqual(project_n.tidy_key(Path("build/moved"), self.UNIT), key)
        keys = {key}

        def changed():
            keys.add(project_n.tidy_key(self.BDIR, self.UNIT))
            return len(keys)

        self.text = b'# 1 "src/a.cpp"\nint y;\n'
        self.assertEqual(changed(), 2)
        self.compdb(["-O2"])
        self.assertEqual(changed(), 3)
        self.version = "LLVM version 22.1.1\n"
        self.assertEqual(changed(), 4)
        (self.root / "src/.clang-tidy").write_text("Checks: '-*'\n")
        self.assertEqual(changed(), 5)
        # A comment the preprocessed text drops (a NOLINT) in a file of ours it names.
        (self.root / self.UNIT).write_text("int x; // NOLINT\n")
        self.assertEqual(changed(), 6)
        # What lint passes clang-tidy besides the unit.
        with mock.patch.object(project_n, "tidy_command", lambda bdir, unit: ["clang-tidy", "--checks=-*", "-p", bdir, unit]):
            self.assertEqual(changed(), 7)

    def test_the_order_of_a_units_entries_is_no_input(self):
        # XMake exports a file two targets compile (tests/unit/gfx/main.cpp) in either order from one run to the next.
        self.compdb(["-Isrc"], ["-Itests"])
        key = project_n.tidy_key(self.BDIR, self.UNIT)
        self.compdb(["-Itests"], ["-Isrc"])
        self.assertEqual(project_n.tidy_key(self.BDIR, self.UNIT), key)

    def test_a_hit_marks_the_result_used(self):
        # Pruning before CI saves the folder keeps the results this run read (prune-build-cache).
        self.lint()
        stored = next(self.cache.glob("*.json"))
        os.utime(stored, (1, 1))
        self.lint()
        self.assertGreater(stored.stat().st_mtime, time.time() - 60)
        self.assertEqual(self.ran, [self.UNIT])

    def test_a_lint_run_marks_its_start_once(self):
        with contextlib.redirect_stdout(io.StringIO()):
            project_n.tidy_units(self.BDIR, [self.UNIT], False, self.cache)
            mark = (self.cache / project_n.TIDY_RUN_MARK).read_text()
            self.assertAlmostEqual(float(mark), time.time(), delta=60)
            project_n.tidy_units(self.BDIR, [self.UNIT], False, self.cache)  # a second lint keeps the run's start
        self.assertEqual((self.cache / project_n.TIDY_RUN_MARK).read_text(), mark)

    def test_main_reads_no_cached_result(self):
        self.lint()
        self.output = ("", 0)
        with self.ci("refs/heads/main"):
            self.assertEqual(self.lint(), ("", 0))
        self.assertEqual(self.ran, [self.UNIT, self.UNIT])
        self.assertEqual(self.lint(), ("", 0))  # main stored its result: the next branch run reads it
        self.assertEqual(len(self.ran), 2)

    def test_a_tag_stores_nothing(self):
        with self.ci("refs/tags/v1.0.0", self.root / "objects"):
            self.assertIsNone(project_n.tidy_cache_dir(self.BDIR))
            self.lint()
            self.lint()
        self.assertEqual(len(self.ran), 2)
        self.assertFalse(self.cache.exists())

    def test_cache_folders(self):
        self.assertEqual(project_n.tidy_cache_dir(self.BDIR), self.root / self.BDIR / ".tidy-cache")
        with self.ci("refs/heads/ci-speed", self.root / "objects"):
            self.assertEqual(project_n.tidy_cache_dir(self.BDIR), self.root / "objects" / "tidy")

    def test_a_corrupt_result_is_a_miss(self):
        self.lint()
        next(self.cache.glob("*.json")).write_text("{\"output\": ")
        self.assertEqual(self.lint(), self.output)
        self.assertEqual(len(self.ran), 2)

    def test_a_unit_whose_markers_miss_its_own_file_is_never_cached(self):
        # ROOT spelt otherwise in the markers (a symlink, a bind mount): no file of ours would count, nor a NOLINT edit.
        self.text = b'# 1 "/elsewhere/src/a.cpp"\nint x;\n'
        self.assertIsNone(project_n.tidy_key(self.BDIR, self.UNIT))

    def test_a_unit_that_fails_to_preprocess_is_never_cached(self):
        self.text = None
        self.assertIsNone(project_n.tidy_key(self.BDIR, self.UNIT))
        self.lint()
        self.lint()
        self.assertEqual(len(self.ran), 2)
        self.assertFalse(self.cache.exists())

    def test_a_crash_is_never_cached(self):
        # A crash may be the machine's (memory, a signal), not the unit's: the next run tries again.
        self.output = ("", -11)
        self.lint()
        self.lint()
        self.assertEqual(len(self.ran), 2)

    def test_preprocessing_drops_the_object_and_adds_e(self):
        entry = {"directory": "/r", "file": "src/a.cpp",
                 "arguments": ["/usr/bin/clang++", "-c", "-Isrc", "-o", "build/a.o", "src/a.cpp"]}
        flags, command = project_n.preprocess_command(entry)
        self.assertEqual(flags, ["/usr/bin/clang++", "-Isrc"])
        self.assertEqual(command, ["/usr/bin/clang++", "-Isrc", "-E", "src/a.cpp"])

    def test_a_failing_cached_unit_is_still_fixed(self):
        fixed = []
        with mock.patch.object(project_n, "tidy_fix", lambda bdir, units: fixed.append(units)), \
                contextlib.redirect_stdout(io.StringIO()) as out:
            project_n.tidy_units(self.BDIR, [self.UNIT], False, self.cache)
            verdict = project_n.tidy_units(self.BDIR, [self.UNIT], True, self.cache)
        self.assertEqual(self.ran, [self.UNIT])
        self.assertEqual(verdict, (True, 0))
        self.assertEqual(fixed, [[self.UNIT]])
        self.assertEqual(out.getvalue(), self.FINDING * 2)  # a cached result prints as a fresh one does


if __name__ == "__main__":
    unittest.main()
