"""Self-checks for project_n.py. Run: pixi run python -m unittest discover -s tools"""

import argparse
import concurrent.futures
import contextlib
import copy
import fnmatch
import functools
import importlib.util
import io
import json
import math
import os
import re
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

import project_n


def setUpModule():
    # As if this run had swept already: no test may sweep this checkout's own build/. BuildSweepTest's runs clear
    # it, with ROOT in a temporary folder.
    patch = mock.patch.object(project_n, "swept", True)
    patch.start()
    unittest.addModuleCleanup(patch.stop)


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
                if isinstance(text, bytes):
                    (root / name).write_bytes(text)
                else:
                    (root / name).write_text(text, encoding="utf-8")
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

    def test_the_summary_says_which_sanitizer_check_only_built(self):
        built = ["windows-msvc's address"]
        self.assertEqual(project_n.check_summary(True, [], built),
                         "check: all presets passed with every supported sanitizer; windows-msvc's address built, not run "
                         "(MSVC sanitizer runs need native Windows)")
        self.assertEqual(project_n.check_summary(True, [], []), "check: all presets passed with every supported sanitizer")
        self.assertEqual(project_n.check_summary(False, [], []), "check: all presets passed")
        self.assertEqual(project_n.check_summary(False, ["macos-clang"], []),
                         "check: all presets passed (skipped macos-clang: not buildable on this host)")

    def test_the_summary_joins_what_only_built_and_what_was_skipped(self):
        self.assertEqual(project_n.check_summary(True, ["macos-clang"], ["windows-msvc's address"]),
                         "check: all presets passed with every supported sanitizer; windows-msvc's address built, not run "
                         "(MSVC sanitizer runs need native Windows); skipped macos-clang: not buildable on this host")

    def test_a_sanitizer_check_only_builds_for_msvc_off_windows(self):
        with mock.patch.object(project_n, "HOST", "linux"):
            self.assertEqual(project_n.sanitizer_builds_only([]), ["windows-msvc's address"])
            self.assertEqual(project_n.sanitizer_builds_only(["windows-msvc"]), [])
        with mock.patch.object(project_n, "HOST", "windows"):
            self.assertEqual(project_n.sanitizer_builds_only([]), [])

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
        expected = ["selftest", "fmt", "lint", *(f"test {p} release" for p in presets), "reference",
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

    def test_check_runs_no_unity_step(self):
        tests, builds, references, out = self.container_check()
        self.assertNotIn("unity", out)
        self.assertFalse([args for args in tests + builds + references if hasattr(args, "unity")])

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

    def timed_out(self, output):
        """Runs one case whose process outlives its bound, leaving `output` as what it printed; returns the result,
        the timeout it was given and its runtime directory."""
        seen = {}

        def fake_run(argv, **kwargs):
            seen["timeout"], seen["dir"] = kwargs["timeout"], Path(kwargs["env"]["XDG_RUNTIME_DIR"])
            raise subprocess.TimeoutExpired(argv, kwargs["timeout"], output=output)
        with mock.patch.object(project_n.subprocess, "run", fake_run):
            result = project_n.run_reference_case("exe", {}, 7)
        return result, seen["timeout"], seen["dir"]

    def test_a_case_past_the_bound_fails_and_says_so(self):
        (output, returncode), timeout, directory = self.timed_out(b"partial\n")
        self.assertEqual(timeout, project_n.REFERENCE_CASE_TIMEOUT)
        self.assertNotEqual(returncode, 0)
        self.assertIn("partial", output)
        self.assertIn(f"timed out after {project_n.REFERENCE_CASE_TIMEOUT} s", output)
        self.assertFalse(directory.exists())

    def test_a_timeout_without_output(self):
        for captured in (None, "text"):
            (output, returncode), _, _ = self.timed_out(captured)
            self.assertNotEqual(returncode, 0)
            self.assertIn("timed out after", output)
            if captured:
                self.assertIn(captured, output)


class ReferenceCaseNameTest(unittest.TestCase):
    def name(self, stdout=None, raises=None):
        def fake_run(argv, **kwargs):
            if raises:
                raise raises
            return subprocess.CompletedProcess(argv, 0, stdout)
        with mock.patch.object(project_n.subprocess, "run", fake_run):
            return project_n.reference_case_name("exe", {}, 3)

    def listing(self, *names):
        rule = "=" * 79
        return ("[doctest] listing all test case names\n" + rule + "\n" + "".join(f"{n}\n" for n in names) + rule
                + "\n[doctest] unskipped test cases passing the current filters: 47\n")

    def test_the_one_listed_name_is_returned(self):
        self.assertEqual(self.name(self.listing("the map cells, at zoom 1")), "the map cells, at zoom 1")

    def test_a_listing_of_two_names_names_none(self):
        self.assertIsNone(self.name(self.listing("one", "two")))

    def test_a_listing_that_times_out_names_none(self):
        self.assertIsNone(self.name(raises=subprocess.TimeoutExpired("exe", 30)))


class ReferenceTest(unittest.TestCase):
    def references(self, update, committed=(), case=None, draws=None, quiet=False, count=3, cores=8, case_name=None):
        """Runs `run_references` in a temporary root holding the `committed` pictures, over `count` cases run by `case`
        (index -> (output, return code)) on `cores` cores; by default case 2 draws a.png and fails. Case 1 first draws
        `draws` (name -> bytes). `case_name` stands in for `reference_case_name`. Returns a.png's bytes or None; keeps
        the indices run, the environments they got, stdout and what it returned."""
        ran, out, self.envs = [], io.StringIO(), []

        def draw_and_fail(index):
            if index == 2:
                (root / "build" / "reference" / "a.png").write_bytes(b"drawn")
                return "case 2 failed\n", 1
            return "", 0

        def run_case(exe, env, index):
            ran.append(index)
            self.envs.append(env)
            for name, data in (draws or {}).items() if index == 1 else ():
                (root / "build" / "reference" / name).write_bytes(data)
            return (case or draw_and_fail)(index)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "tests" / "reference").mkdir(parents=True)
            for name in committed:
                (root / "tests" / "reference" / name).write_bytes(b"committed")
            try:
                with mock.patch.multiple(project_n, ROOT=root, capture=lambda argv: '{"environments_info": [{"name": "lavapipe", "prefix": "/lavapipe"}]}',
                                         run=lambda argv, env=None: None, reference_case_count=lambda exe, env: count,
                                         run_reference_case=run_case, binary_path=lambda *args: Path("reference_tests"),
                                         jobs=lambda: cores,
                                         reference_case_name=case_name or (lambda exe, env, index: None)), \
                     contextlib.redirect_stdout(out):
                    self.returned = project_n.run_references("linux-gcc", update, quiet=quiet)
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

    def test_a_timed_out_case_is_named(self):
        def case(index):
            return (f"reference: case 2 timed out after {project_n.REFERENCE_CASE_TIMEOUT} s\n", -9) if index == 2 else ("", 0)
        with self.assertRaises(subprocess.CalledProcessError) as raised:
            self.references(update=False, case=case, case_name=lambda exe, env, index: "world_route" if index == 2 else None)
        self.assertIn("world_route", raised.exception.stderr)
        with self.assertRaises(subprocess.CalledProcessError) as raised:
            self.references(update=False, case=case)
        self.assertIn("case 2", raised.exception.stderr)

    def test_each_case_gets_its_share(self):
        for cores, count, share in ((32, 47, "1"), (8, 3, "2")):
            self.references(update=False, case=lambda index: ("", 0), count=count, cores=cores)
            self.assertEqual(len(self.envs), count)
            for env in self.envs:
                self.assertEqual(env["LP_NUM_THREADS"], share)
                self.assertEqual(env["PROJECT_N_REFERENCE_WORKERS"], share)

    def test_update_refuses_after_a_timeout(self):
        with self.assertRaises(project_n.Fail):
            self.references(update=True, committed=["a.png", "b.png"],
                            case=lambda index: ("timed out\n", -9) if index == 2 else ("", 0))

    def test_outputs_print_in_case_order(self):
        def finish_out_of_order(index):
            time.sleep(0.01 * (3 - index))  # case 3 finishes first
            return ["one", "two", "three"][index - 1], 0
        self.references(update=False, case=finish_out_of_order)
        self.assertLess(self.out.index("one"), self.out.index("two"))
        self.assertLess(self.out.index("two"), self.out.index("three"))

    def test_a_quiet_run_returns_the_outputs_and_prints_them_only_on_a_failure(self):
        self.references(update=False, case=lambda index: (f"MESSAGE: case {index}\n", 0), quiet=True)
        self.assertEqual(self.returned, "MESSAGE: case 1\nMESSAGE: case 2\nMESSAGE: case 3\n")
        self.assertNotIn("MESSAGE", self.out)
        with self.assertRaises(subprocess.CalledProcessError):
            self.references(update=False, case=lambda index: (f"MESSAGE: case {index}\n", 1 if index == 2 else 0),
                            quiet=True)
        self.assertIn("MESSAGE: case 1\nMESSAGE: case 2\nMESSAGE: case 3\n", self.out)

    def test_a_passing_run_names_the_pictures_that_changed_within_tolerance(self):
        # Lavapipe draws every case the same to the byte, so a picture that differs has drifted from its reference.
        self.references(update=False, committed=["a.png", "b.png", "c.png"], case=lambda index: ("", 0),
                        draws={"a.png": b"drawn", "b.png": b"committed", "c.png": b"drawn"})
        self.assertIn("reference: within tolerance but changed: a, c; refresh them with "
                      "`pixi run project_n reference --update`", self.out)

    def test_a_passing_run_that_drew_every_reference_says_nothing_more(self):
        self.references(update=False, committed=["a.png"], case=lambda index: ("", 0), draws={"a.png": b"committed"})
        self.assertNotIn("changed", self.out)

    def test_update_doesnt_name_changed_pictures(self):
        self.references(update=True, committed=["a.png"], case=lambda index: ("", 0), draws={"a.png": b"drawn"})
        self.assertNotIn("changed", self.out)

    def test_orphan_pictures_are_the_committed_names_no_case_drew_sorted(self):
        with tempfile.TemporaryDirectory() as tmp:
            committed, drawn = Path(tmp) / "committed", Path(tmp) / "drawn"
            for folder, names in ((committed, ["z.png", "a.png", "kept.png", "notes.txt"]), (drawn, ["kept.png", "new.png"])):
                folder.mkdir()
                for name in names:
                    (folder / name).write_bytes(b"x")
            self.assertEqual(project_n.orphan_pictures(committed, drawn), ["a.png", "z.png"])
            self.assertEqual(project_n.orphan_pictures(committed, committed), [])

    def test_a_passing_run_fails_on_committed_pictures_no_case_drew(self):
        with self.assertRaises(project_n.Fail) as raised:
            self.references(update=False, committed=["b.png", "a.png", "c.png"], case=lambda index: ("", 0),
                            draws={"c.png": b"committed"})
        self.assertEqual(str(raised.exception), "reference: no case drew a.png, b.png: delete them or restore their case")

    def test_a_failed_case_is_reported_alone_never_as_an_orphan_too(self):
        with self.assertRaises(subprocess.CalledProcessError) as raised:  # case 2 failed before drawing b.png
            self.references(update=False, committed=["a.png", "b.png"], case=lambda index: ("", 1 if index == 2 else 0),
                            draws={"a.png": b"committed"})
        self.assertIn("1 of 3 case(s) failed", raised.exception.stderr)
        self.assertNotIn("no case drew", raised.exception.stderr)
        self.assertNotIn("no case drew", self.out)

    def test_update_keeps_its_own_message_for_pictures_not_drawn(self):
        with self.assertRaises(project_n.Fail) as raised:
            self.references(update=True, committed=["a.png", "b.png"], case=lambda index: ("", 0), draws={"a.png": b"x"})
        self.assertEqual(str(raised.exception), "reference: nothing updated; the run didn't draw b.png")

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

        def stop(*args, **kwargs):
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
    """CI's object cache: warm on branch pushes, never read on main or a tag (a release is always built cold)."""

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
            self.assertEqual(project_n.object_cache_dir("linux-clang", "debug", "address"),
                             self.tmp / "c" / "linux-clang-debug-address")
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

        def stop(*args, **kwargs):
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

        def stop(*args, **kwargs):
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


class MeasureTest(unittest.TestCase):
    SEP = project_n.EXPORT_SEP
    HEADER = SEP.join(("name", "src_file", "src_line", "ns_since_start", "exec_time_ns", "thread", "value")) + "\n"
    MS = 1_000_000

    def zones(self, *rows):
        """`tracy-csvexport -u -e -s EXPORT_SEP` lines from (name, file, start ms, self ms, thread) rows."""
        return [self.HEADER, *(self.SEP.join((name, file, "1", str(round(start * self.MS)), str(round(took * self.MS)),
                                              str(thread), "")) + "\n"
                               for name, file, start, took, thread in rows)]

    def messages(self, *rows):
        """`tracy-csvexport -m -s EXPORT_SEP` output from (text, ns) rows."""
        return "".join(f"{text}{self.SEP}{ns}\n" for text, ns in (("MessageName", "total_ns"), *rows))

    def frames(self, *starts):
        return [("Run", "src/app/app.cpp", start, 0.1, 1) for start in starts]

    def test_percentiles(self):
        self.assertEqual(project_n.percentiles(list(range(100, 0, -1))), (50.5, 99, 100))
        self.assertEqual(project_n.percentiles([*[1.0] * 197, 5.0, 6.0, 40.0]), (1.0, 5.0, 40.0))
        # Below 100 values the 99th percentile by nearest rank is the worst: none is given.
        self.assertEqual(project_n.percentiles([2.0, 9.0, 1.0, 1.5, 30.0]), (2.0, None, 30.0))
        self.assertEqual(project_n.percentiles([4.0]), (4.0, None, 4.0))

    def test_zone_rows_keep_names_and_texts_holding_commas(self):
        # MSVC's function names keep template arguments; a zone's text is free.
        line = self.SEP.join(("Pack<A,B>::Load", "src/gfx/renderer.cpp", "9", "500", "20", "3", "world, tiles")) + "\n"
        self.assertEqual(list(project_n.zone_rows([self.HEADER, line])),
                         [("Pack<A,B>::Load", "src/gfx/renderer.cpp", 500, 20, "3")])

    def test_hitches_use_work_not_wall_time(self):
        # Frame 1 is 30 ms long only by its 25 ms wait for the GPU; frame 2 is 20 ms of work.
        timeline = project_n.frame_timeline(self.zones(
            *self.frames(0, 10, 40, 60), ("WaitForGpu", "src/gfx/internal/frame/frame.cpp", 12, 25, 1),
            ("WaitForGpu", "src/gfx/internal/frame/frame.cpp", 59, 0.5, 1), ("Update", "src/core/logic.cpp", 41, 19, 2)))
        self.assertEqual((timeline.main, len(timeline.starts)), ("1", 4))
        self.assertEqual(project_n.hitch_frames(timeline, range(3)), [2])
        self.assertEqual(round(timeline.work(1) / self.MS, 3), 5.0)

    def test_segments_split_by_messages(self):
        messages = project_n.read_messages(self.messages(("perf_event_paranoid: 2", 1000), ("camera path: follow", 2_000_000_000),
                                                         ("camera path: zoom in, then out", 6_000_000_000)))
        self.assertEqual(messages[-1], ("camera path: zoom in, then out", 6_000_000_000))
        self.assertEqual(project_n.measure_segments(messages, 0, 8_000_000_000, camera=True),
                         [("follow: first 0.5 s", 2_000_000_000, 2_500_000_000), ("follow: rest", 2_500_000_000, 6_000_000_000),
                          ("zoom in, then out: first 0.5 s", 6_000_000_000, 6_500_000_000),
                          ("zoom in, then out: rest", 6_500_000_000, 8_000_000_000)])
        # The month step's message starts a segment of its own inside the one it falls in.
        stepped = project_n.read_messages(self.messages(("camera path: widest view", 12_000_000_000),
                                                        ("camera path: month step", 15_000_000_000),
                                                        ("camera path: labels", 18_000_000_000)))
        self.assertEqual(project_n.measure_segments(stepped, 0, 22_000_000_000, camera=True),
                         [("widest view: first 0.5 s", 12_000_000_000, 12_500_000_000),
                          ("widest view: rest", 12_500_000_000, 15_000_000_000),
                          ("month step: first 0.5 s", 15_000_000_000, 15_500_000_000),
                          ("month step: rest", 15_500_000_000, 18_000_000_000),
                          ("labels: first 0.5 s", 18_000_000_000, 18_500_000_000),
                          ("labels: rest", 18_500_000_000, 22_000_000_000)])
        # An arena run has no path: one segment, after the scene has loaded.
        self.assertEqual(project_n.measure_segments(messages, 1_000_000_000, 9_000_000_000, camera=False),
                         [("whole run", 3_000_000_000, 9_000_000_000)])
        # A segment shorter than the new-ground part is all new ground; its rest is empty.
        short = project_n.read_messages(self.messages(("camera path: pan", 1_000_000_000), ("camera path: labels", 1_300_000_000)))
        self.assertEqual(project_n.measure_segments(short, 0, 2_000_000_000, camera=True)[:2],
                         [("pan: first 0.5 s", 1_000_000_000, 1_300_000_000), ("pan: rest", 1_300_000_000, 1_300_000_000)])
        timeline = project_n.frame_timeline(self.zones(*self.frames(1000, 1100, 1200, 1300, 1400)))
        self.assertFalse(project_n.segment_frames(timeline, 1_300_000_000, 1_300_000_000))

    def test_segment_rows_count_each_thread_and_the_gpus_share(self):
        timeline = project_n.frame_timeline(self.zones(
            *self.frames(0, 10, 20, 30), ("WaitForGpu", "src/gfx/internal/frame/frame.cpp", 5, 4, 1),
            ("WaitForGpu", "src/gfx/internal/frame/frame.cpp", 15, 6, 1)))
        work = project_n.frame_work(self.zones(
            *self.frames(0, 10, 20, 30), ("Present", "src/app/app.cpp", 1, 3, 1), ("WaitForGpu", "src/gfx/internal/frame/frame.cpp", 5, 4, 1),
            ("Tick", "src/core/logic.cpp", 11, 2, 2), ("WaitUntilNs", "src/platform/clock.cpp", 13, 7, 2),
            ("ExecuteRange", "src/core/jobs.cpp", 12, 1, 5)), timeline, hitches=[])
        segment = project_n.segment_stats("all", timeline, work, range(3), hitches=[])
        rows = {what: (round(median, 2), p99, round(worst, 2)) for what, (median, p99, worst) in segment.rows}
        self.assertEqual(rows["frame"], (10.0, None, 10.0))
        self.assertEqual(rows["work"], (6.0, None, 10.0))
        self.assertEqual(rows["main"], (0.1, None, 3.1))  # its waits for the GPU and the clock aren't work
        self.assertEqual(rows["logic"], (0.0, None, 2.0))
        self.assertEqual(rows["worker 5"], (0.0, None, 1.0))
        self.assertAlmostEqual(segment.gpu, 1 / 3)

    def test_threads_are_named_by_what_they_ran(self):
        timeline = project_n.frame_timeline(self.zones(*self.frames(0, 10)))
        work = project_n.frame_work(self.zones(
            ("Mix", "src/platform/audio.cpp", 1, 1, 7), ("ExecuteRange", "src/core/jobs.cpp", 1, 1, 12),
            ("Tick", "src/core/logic.cpp", 1, 1, 9), ("ExecuteRange", "src/core/jobs.cpp", 1, 1, 9),
            ("ExecuteRange", "src/core/jobs.cpp", 1, 1, 3), ("Tick", "src/core/logic.cpp", 1, 1, 2),
            ("ExecuteRange", "src/core/jobs.cpp", 1, 1, 1), ("Present", "src/app/app.cpp", 1, 1, 1)),
            timeline, hitches=[])
        # The main and logic threads run job system zones too; two logic threads keep a name each.
        self.assertEqual(list(work.busy), ["main", "logic 2", "logic 9", "worker 3", "worker 12", "thread 7"])
        self.assertEqual([busy[0] / self.MS for busy in work.busy.values()], [2, 1, 2, 1, 1, 1])

    def test_a_segment_paced_by_the_display_is_marked(self):
        def segment(median):
            return project_n.Segment("all", 200, [("frame", (median, 7.0, 8.0))], 0.87, [])
        self.assertTrue(project_n.paced(segment(6.06), "2560x1440 at 165 Hz"))
        self.assertFalse(project_n.paced(segment(4.0), "2560x1440 at 165 Hz"))
        self.assertFalse(project_n.paced(segment(6.06), "unknown"))
        table = project_n.measure_table([project_n.MeasuredRun("burst", "2560x1440 at 165 Hz", [], [], [segment(6.06)]),
                                         project_n.MeasuredRun("swords", "2560x1440 at 165 Hz", [], [], [segment(1.2)])],
                                        "not measured (no world run)", "not measured (no world run)",
                                        "not measured (no world run)")
        rows = {line.split()[0]: line for line in table.splitlines() if line.startswith(("burst", "swords"))}
        self.assertIn("87% paced", rows["burst"])
        self.assertNotIn("paced", rows["swords"])
        self.assertIn("at a volley's peak", rows["swords"])

    def test_a_hitch_names_its_largest_zones(self):
        timeline = project_n.frame_timeline(self.zones(*self.frames(0, 30)))
        work = project_n.frame_work(self.zones(
            ("Upload", "src/gfx/renderer.cpp", 1, 12, 1), ("Place", "src/gfx/labels.cpp", 2, 3, 1),
            ("Place", "src/gfx/labels.cpp", 6, 3, 1), ("Draw", "src/gfx/renderer.cpp", 9, 4, 1),
            ("Tiny", "src/gfx/renderer.cpp", 14, 1, 1), ("WaitForGpu", "src/gfx/internal/frame/frame.cpp", 20, 9, 1),
            ("Busy", "src/core/logic.cpp", 3, 25, 2)), timeline, hitches=[0])
        segment = project_n.segment_stats("all", timeline, work, range(1), hitches=[0])
        (start, took, frame, zones), = segment.hitches
        self.assertEqual((start, round(took / self.MS), round(frame / self.MS)), (0, 30, 30))
        self.assertEqual([(name, round(ns / self.MS)) for name, ns in zones], [("Upload", 12), ("Place", 6), ("Draw", 4)])

    def test_presses_and_display_delay_are_summarised(self):
        self.assertEqual(project_n.latency_summary([31.1, 23.0, 25.5]),
                         "input latency median 25.5 ms, 23.0 to 31.1 ms over 3 presses")
        self.assertEqual(project_n.latency_summary([31.1]), "input latency 31.1 ms over 1 press")
        self.assertIsNone(project_n.latency_summary([]))
        log = "[1] app.cpp:1  LOG_INFO  root  display delay: 2.9 ms at the end, 2.6 to 10.0 ms over the run\n"
        self.assertEqual(project_n.display_delay(log), (2.9, 2.6, 10.0))
        self.assertIsNone(project_n.display_delay("no delay line\n"))
        steady = project_n.Segment("whole run", 10, [("frame", (1.0, 2.0, 3.0))], 0.25, [])
        run = project_n.MeasuredRun("world", "2560x1440 at 165 Hz", [31.1, 23.0, 25.5], [], [steady], (2.9, 2.6, 10.0))
        row = next(line for line in project_n.measure_table([run], "x", "y", "z").splitlines() if line.startswith("world"))
        self.assertIn("input latency median 25.5 ms, 23.0 to 31.1 ms over 3 presses", row)
        self.assertIn("display delay 2.9 ms (2.6 to 10.0)", row)
        self.assertNotIn("input latency 23", row)
        self.assertNotIn("input latency 25.5 ms", row)
        self.assertNotIn("input latency 31.1 ms", row)

    def test_latency_read_from_log(self):
        log = ("21:00:01.1 [1] app.cpp:338   LOG_INFO   root   input latency: 31.4 ms from a Move press to its first drawn frame\n"
               "21:00:02.1 [1] app.cpp:330   LOG_DEBUG  root   swords first drawn 12.0 ms after the press\n"
               "21:00:03.1 [1] app.cpp:338   LOG_INFO   root   input latency: 8.0 ms from a Move press to its first drawn frame\n")
        self.assertEqual(project_n.input_latencies(log), [31.4, 8.0])
        self.assertEqual(project_n.input_latencies("display: 2560x1440 at 165 Hz\n"), [])
        self.assertEqual(project_n.display_mode("[1] window.cpp:234  LOG_INFO  root  display: 2560x1440 at 165 Hz\n"),
                         "2560x1440 at 165 Hz")
        self.assertEqual(project_n.display_mode("no display line\n"), "unknown")

    def test_display_is_the_one_the_window_ended_on(self):
        log = ("[1] window.cpp:290  LOG_INFO  root  display: 1920x1080 at 59.95 Hz (DP-1)\n"
               "[1] window.cpp:290  LOG_INFO  root  display: 2560x1440 at 165 Hz (DP-2)\n")
        self.assertEqual(project_n.display_mode(log), "2560x1440 at 165 Hz (DP-2)")
        segment = project_n.Segment("follow", 200, [("frame", (6.06, 6.2, 6.3))], 0.9, [])
        self.assertTrue(project_n.paced(segment, project_n.display_mode(log)))

    def test_shimmer_read_from_reference_output(self):
        output = ("[doctest] doctest version is \"2.4.11\"\n"
                  "tests/reference/reference_tests.cpp:212: MESSAGE: world shimmer: 0.0308694\n"
                  "tests/reference/reference_tests.cpp:180: MESSAGE: seals shimmer: 0.01\n")
        self.assertEqual(project_n.world_shimmer(output), 0.0308694)
        self.assertIsNone(project_n.world_shimmer("no such case\n"))

    def test_cloud_shimmer_read_from_reference_output(self):
        output = ("tests/reference/reference_tests.cpp:212: MESSAGE: world shimmer: 0.0308694\n"
                  "tests/reference/reference_tests.cpp:300: MESSAGE: cloud shimmer: 0.0204705\n")
        self.assertEqual(project_n.cloud_shimmer(output), 0.0204705)
        self.assertEqual(project_n.world_shimmer(output), 0.0308694)
        self.assertIsNone(project_n.cloud_shimmer("world shimmer: 0.03\n"))

    def test_ground_shimmer_read_from_reference_output_by_zoom(self):
        output = ("tests/reference/reference_tests.cpp:212: MESSAGE: world shimmer: 0.0308694\n"
                  "tests/reference/reference_tests.cpp:300: MESSAGE: cloud shimmer: 0.0204705\n"
                  "tests/reference/reference_tests.cpp:340: MESSAGE: ground shimmer 1: 0.0123\n"
                  "tests/reference/reference_tests.cpp:350: MESSAGE: ground shimmer 0.5: 0.0101\n"
                  "tests/reference/reference_tests.cpp:360: MESSAGE: ground shimmer 0.25: 0.00875\n")
        self.assertEqual(project_n.ground_shimmer(output), [("1", 0.0123), ("0.5", 0.0101), ("0.25", 0.00875)])
        self.assertEqual(project_n.world_shimmer(output), 0.0308694)
        self.assertEqual(project_n.cloud_shimmer(output), 0.0204705)
        self.assertEqual(project_n.ground_shimmer("world shimmer: 0.03\ncloud shimmer: 0.02\n"), [])

    def test_the_world_run_turns_the_debug_sight_on(self):
        self.assertIn("--debug-sight", project_n.MEASURE_RUNS["world"][0])

    def test_measure_refuses_unknown_runs(self):
        def build(*args):
            raise AssertionError("built before refusing the runs")
        with mock.patch.multiple(project_n, build=build):
            with self.assertRaisesRegex(project_n.Fail, "unknown run.*rain.*world, burst, swords, seals"):
                project_n.cmd_measure(argparse.Namespace(runs="world,rain", keep=False))
            with self.assertRaises(project_n.Fail):
                project_n.cmd_measure(argparse.Namespace(runs="", keep=False))
        self.assertEqual(project_n.measure_runs("seals,world,seals"), ["seals", "world"])
        self.assertEqual(project_n.measure_runs(None), ["world", "burst", "swords", "seals"])

    def test_world_run_raises_the_travel_share_the_busiest_view_case_counts_at(self):
        args = project_n.MEASURE_RUNS["world"][0]
        self.assertEqual(args[args.index("--travel-share") + 1], str(project_n.MEASURE_TRAVEL_SHARE))
        case = (project_n.ROOT / "tests" / "unit" / "game" / "shipped_world_tests.cpp").read_text(encoding="utf-8")
        shares = re.findall(r"constexpr u16 MEASURED_SHARE = (\d+);", case)
        self.assertEqual(shares, [str(project_n.MEASURE_TRAVEL_SHARE)])

    def test_world_run_steps_a_month_at_the_second_the_camera_path_test_checks(self):
        args = project_n.MEASURE_RUNS["world"][0]
        self.assertEqual(args[args.index("--month-step-at") + 1], str(project_n.MEASURE_MONTH_STEP_AT))
        case = (project_n.ROOT / "tests" / "unit" / "app" / "camera_path_tests.cpp").read_text(encoding="utf-8")
        seconds = re.findall(r"constexpr f32 MEASURED = ([\d.]+)F;", case)
        self.assertEqual([float(second) for second in seconds], [float(project_n.MEASURE_MONTH_STEP_AT)])

    PAUSED_LOG = ("21:00:00.1 [1] window.cpp:234  LOG_INFO     root  display: 2560x1440 at 165 Hz\n"
                  "21:00:05.1 [1] app.cpp:403     LOG_INFO     root  paused: the window is unfocused or minimised\n"
                  "21:00:05.1 [1] app.cpp:406     LOG_WARNING  root  the game paused during the camera path: from here it "
                  "measures no running game\n"
                  "21:00:06.9 [1] app.cpp:315     LOG_INFO     root  camera path: pan\n"
                  "21:00:06.9 [1] app.cpp:318     LOG_WARNING  root  camera path: pan with the game paused (the window "
                  "is unfocused or minimised): it measures no running game\n"
                  "21:00:09.1 [1] app.cpp:403     LOG_INFO     root  resumed: the window is back\n"
                  "21:00:25.1 [1] app.cpp:305     LOG_INFO     root  camera path finished: quitting\n")

    def test_paused_run_is_untrusted(self):
        self.assertEqual(project_n.untrusted_reasons(self.PAUSED_LOG, camera=True),
                         ["paused: the window is unfocused or minimised",
                          "the game paused during the camera path: from here it measures no running game",
                          ("camera path: pan with the game paused (the window is unfocused or minimised): it measures no "
                           "running game")])
        self.assertEqual(project_n.untrusted_reasons("camera path finished: quitting\n", camera=True), [])
        self.assertEqual(project_n.untrusted_reasons("quitting after 20 seconds\n", camera=False), [])
        self.assertEqual(project_n.untrusted_reasons("quitting after 60 seconds\n", camera=True),
                         ["the camera path didn't finish"])
        steady = project_n.Segment("whole run", 10, [("frame", (1.0, 2.0, 3.0))], 0.25, [])
        measured = {"world": project_n.MeasuredRun("world", "2560x1440 at 165 Hz", [31.4], ["paused: the window is unfocused or minimised"], [steady]),
                    "burst": project_n.MeasuredRun("burst", "2560x1440 at 165 Hz", [], [], [steady])}
        asked = []

        def run_references(preset, quiet=False):
            asked.append(quiet)
            return ("[INFO] a case's game log\nMESSAGE: world shimmer: 0.031\nMESSAGE: cloud shimmer: 0.021\n"
                    "MESSAGE: ground shimmer 1: 0.012\nMESSAGE: ground shimmer 0.5: 0.01\n"
                    "MESSAGE: ground shimmer 0.25: 0.008\n")
        status, out, err, left = self.measure(measured, run_references)
        self.assertEqual(asked, [True])  # the cases' output stays out of the table's way
        self.assertNotIn("a case's game log", out)
        self.assertEqual(status, 1)
        world_row = next(line for line in out.splitlines() if line.startswith("world "))
        self.assertIn("untrusted: paused: the window is unfocused or minimised", world_row)
        self.assertNotIn("untrusted", next(line for line in out.splitlines() if line.startswith("burst ")))
        self.assertIn("world shimmer: 0.031", out)
        self.assertIn("cloud shimmer: 0.021", out)
        header = next(line for line in out.splitlines() if line.startswith(f"measure: {project_n.MEASURE_WORKERS} workers"))
        self.assertIn("ground shimmer: 0.012 at zoom 1, 0.01 at zoom 0.5, 0.008 at zoom 0.25", header)
        self.assertIn("1 of 2 runs untrusted: world", err)
        self.assertEqual(len(left), 1)  # kept even without --keep: the game log says why it is untrusted
        self.assertIn(f"game logs are kept in {left[0]}", out)

    def measure(self, measured, run_references):
        """Runs `cmd_measure` over the runs `measured` (name -> MeasuredRun) with `run_references` in place of the
        reference pictures; returns (its status, stdout, stderr, what is left in build/measure/)."""
        with tempfile.TemporaryDirectory() as tmp, \
             mock.patch.multiple(project_n, ROOT=Path(tmp), HOST="linux", build=lambda *args: Path("build"),
                                 run_references=run_references,
                                 profiler_tool=lambda program, package: f"/profiler/bin/{program}",
                                 measure_run=lambda name, bdir, tools, folder: measured[name]), \
             contextlib.redirect_stdout(io.StringIO()) as out, contextlib.redirect_stderr(io.StringIO()) as err:
            status = project_n.cmd_measure(argparse.Namespace(runs=",".join(measured), keep=False))
            left = list((Path(tmp) / "build" / "measure").glob("*"))
        return status, out.getvalue(), err.getvalue(), left

    def test_a_missing_cloud_shimmer_is_reported_apart_from_the_world_s(self):
        def world_only(preset, quiet=False):
            return "MESSAGE: world shimmer: 0.031\n"
        steady = project_n.Segment("whole run", 10, [("frame", (1.0, None, 3.0))], 0.25, [])
        out = self.measure({"world": project_n.MeasuredRun("world", "unknown", [], [], [steady])}, world_only)[1]
        self.assertIn("world shimmer: 0.031 (the reference pictures' world case); "
                      "cloud shimmer: not measured (the reference pictures printed none); "
                      "ground shimmer: not measured (the reference pictures printed none)", out)

    def test_a_failing_reference_case_leaves_the_shimmer_unmeasured_and_runs_on(self):
        def fail(preset, quiet=False):
            raise subprocess.CalledProcessError(1, ["reference_tests"], stderr="reference: 1 of 44 case(s) failed: world\n")
        steady = project_n.Segment("whole run", 10, [("frame", (1.0, None, 3.0))], 0.25, [])
        status, out, err, left = self.measure({"world": project_n.MeasuredRun("world", "unknown", [], [], [steady])}, fail)
        self.assertEqual(left, [])  # a trusted run without --keep leaves nothing
        self.assertIsNone(status)
        self.assertIn("world shimmer: not measured (reference failed); cloud shimmer: not measured (reference failed); "
                      "ground shimmer: not measured (reference failed)", out)
        self.assertIn("world  whole run", out)
        self.assertIn("1 of 44 case(s) failed", err)

    def test_a_camera_run_without_segment_messages_measures_no_frames(self):
        with tempfile.TemporaryDirectory() as tmp:
            export = Path(tmp) / "world.csv"
            export.write_text("".join(self.zones(*self.frames(0, 10, 20, 30))), encoding="utf-8")
            measured = project_n.read_run("world", export, self.messages(("perf_event_paranoid: 2", 1000)),
                                          "display: 2560x1440 at 165 Hz\ncamera path finished: quitting\n")
        self.assertEqual((measured.reasons, measured.segments), (["no frames measured"], []))
        row = next(line for line in project_n.measure_table([measured], "x", "y", "z").splitlines() if line.startswith("world"))
        self.assertIn("display 2560x1440 at 165 Hz; untrusted: no frames measured", row)

    def test_the_game_starts_as_profile_starts_it(self):
        recorded, exported = [], []

        def record(recorder, game, out, **kwargs):
            recorded.append((recorder, game, kwargs["env"]))

        def export(argv, **kwargs):
            exported.append([str(a) for a in argv[:-1]])
            return ""
        with tempfile.TemporaryDirectory() as tmp, \
             mock.patch.multiple(project_n, record=record, run=export, capture=export,
                                 xmake_env=lambda bdir: {"XMAKE_CONFIGDIR": str(bdir)},
                                 read_run=lambda name, export, messages, text: name), \
             contextlib.redirect_stdout(io.StringIO()):
            project_n.measure_run("seals", Path("build/rd"), {"tracy-capture": "capture", "tracy-csvexport": "export"},
                                  Path(tmp))
        self.assertEqual(recorded, [("capture", ["xmake", "run", project_n.APP, "--fullscreen", "--workers", "2", "--unpaced",
                                                 "--quit-after", "30", "--cast-swords", "--zoom", "2"],
                                     {"XMAKE_CONFIGDIR": str(Path("build/rd"))})])
        # Both exports split their columns on a separator no name, text or message holds.
        self.assertEqual(exported, [["export", "-u", "-e", "-s", project_n.EXPORT_SEP], ["export", "-m", "-s", project_n.EXPORT_SEP]])


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


class PathStringsTest(unittest.TestCase):
    def test_path_strings_in_src_are_reported(self):
        with temp_repo({"src/core/a.cpp": "auto a = p.string();\nauto b = p.filename().generic_string();\nauto c = it->path().string();\nauto d = q->string();\n"}):
            self.assertEqual(project_n.path_strings(["src/core/a.cpp"]),
                             ["src/core/a.cpp:1: auto a = p.string();",
                              "src/core/a.cpp:2: auto b = p.filename().generic_string();",
                              "src/core/a.cpp:3: auto c = it->path().string();",
                              "src/core/a.cpp:4: auto d = q->string();"])

    def test_path_strings_in_comments_strings_and_other_folders_are_not(self):
        with temp_repo({"src/core/a.cpp": '// p.string()\nauto a = "x.string()";\nauto b = Row{.string = x};\nauto c = std::string();\n', "tests/unit/b.cpp": "auto b = p.string();\n",
                        "tools/c.cpp": "auto c = p.string();\n"}):
            self.assertEqual(project_n.path_strings(["src/core/a.cpp", "tests/unit/b.cpp", "tools/c.cpp"]), [])


class NonUtf8FilesTest(unittest.TestCase):
    def test_files_not_in_utf8_are_reported(self):
        with temp_repo({"src/a.cpp": b"auto s = \"caf\xe9\";\n", "assets/data/x.yaml": b"\xef\xbb\xbfkey: 1\n"}):
            found = project_n.non_utf8_files(["src/a.cpp", "assets/data/x.yaml"])
            self.assertEqual([f.split(":")[0] for f in found], ["assets/data/x.yaml", "src/a.cpp"])
            self.assertIn("byte-order mark", found[0])

    def test_utf8_files_and_binaries_are_not_reported(self):
        with temp_repo({"src/a.cpp": "auto s = u8\"项目_é_🎮\";\n".encode(), "assets/b.png": b"\x89PNG\xff\xfe\x00\xe9"}):
            self.assertEqual(project_n.non_utf8_files(["src/a.cpp", "assets/b.png", "src/deleted.cpp"]), [])

    def test_extensionless_text_files_are_checked(self):
        with temp_repo({"dev": b"#!/bin/sh\necho caf\xe9\n", ".clangd": b"\xef\xbb\xbfa: 1\n", "docs/Doxyfile": b"A = \xe9\n"}):
            found = project_n.non_utf8_files([".clangd", "dev", "docs/Doxyfile"])
            self.assertEqual([f.split(":")[0] for f in found], [".clangd", "dev", "docs/Doxyfile"])

    def test_lfs_binaries_and_the_fuzz_corpus_are_not_checked(self):
        with temp_repo({".gitattributes": "*.dat filter=lfs diff=lfs merge=lfs -text\n*.bin binary\n", "assets/a.dat": b"\xff\xfe\xe9\n",
                        "tests/fuzz/corpus/yaml/seed": b"\xe9: [\n", "x.bin": b"\xe9\n", "assets/zero": b"\x00\xe9"}):
            self.assertEqual(project_n.non_utf8_files(["assets/a.dat", "tests/fuzz/corpus/yaml/seed", "x.bin", "assets/zero"]), [])


class NonUtf8ListingTest(unittest.TestCase):
    """The listings behind the UTF-8 check give raw names and tell untracked binaries apart."""
    NAME = "src/caf\u00e9.cpp"

    def test_a_non_ascii_name_is_found_by_the_whole_repo_listing(self):
        with temp_repo({self.NAME: b"auto s = \"caf\xe9\";\n"}):
            self.assertEqual(project_n.git_names("ls-files"), [self.NAME])
            found = project_n.non_utf8_files(project_n.git_names("ls-files"))
            self.assertEqual([f.split(":")[0] for f in found], [self.NAME])

    def test_a_listing_with_pathspecs_gives_raw_names(self):
        with temp_repo({"docs/café.md": "a", "src/a.cpp": "a"}):
            self.assertEqual(project_n.git_names("ls-files", "--", "*.md"), ["docs/café.md"])

    def test_a_non_ascii_name_is_found_by_changed_files(self):
        with temp_repo({"src/a.cpp": "a"}) as root:
            (root / self.NAME).write_bytes(b"caf\xe9\n")
            (root / "src" / "b\u00e9.cpp").write_text("b", encoding="utf-8")
            git(root, "add", "src/b\u00e9.cpp")
            changed = project_n.changed_files(None)
            self.assertEqual(changed, {self.NAME, "src/b\u00e9.cpp"})
            self.assertEqual([f.split(":")[0] for f in project_n.non_utf8_files(sorted(changed))], [self.NAME])

    def test_an_untracked_binary_is_not_reported(self):
        with temp_repo({"src/a.cpp": "a"}) as root:
            (root / "assets").mkdir()
            (root / "assets" / "new.dat").write_bytes(b"\x00\xe9\xff")
            (root / "assets" / "new.txt").write_bytes(b"caf\xe9\n")
            found = project_n.non_utf8_files(sorted(project_n.changed_files(None)))
            self.assertEqual([f.split(":")[0] for f in found], ["assets/new.txt"])

    def test_lint_reports_a_non_utf8_source_before_any_step_that_decodes_it(self):
        with temp_repo({"src/a.cpp": b"auto s = \"caf\xe9\";\n"}):
            args = project_n.argparse.Namespace(container=False, fix=False, all=True, range=None)
            with mock.patch.object(project_n, "IN_CONTAINER", True), self.assertRaises(project_n.Fail) as raised:
                project_n.cmd_lint(args)
            self.assertIn("src/a.cpp: not valid UTF-8", str(raised.exception))


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
        self.assertEqual(self.bypassed("#include <enkiTS/TaskScheduler.h>\n", "src/game/host.cpp"),
                         ["src/game/host.cpp:1: #include <enkiTS/TaskScheduler.h>"])
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

    def test_quoted_commands_are_also_checked_in_src_docs(self):
        files = project_n.command_quote_files()
        self.assertIn("src/core/profile.hpp", files)
        self.assertIn("src/app/module.dox", files)

    def test_agent_docs_quote_only_real_commands(self):
        self.assertEqual(project_n.stale_doc_commands(), [])


class DocBudgetsTest(unittest.TestCase):
    def test_an_agents_md_at_the_byte_budget_passes(self):
        self.assertEqual(project_n.doc_budget_problems("src/AGENTS.md", ("a" * 99 + "\n") * 81 + "a" * 91 + "\n"), [])

    def test_an_agents_md_over_the_byte_budget_fails(self):
        self.assertEqual(project_n.doc_budget_problems("src/AGENTS.md", ("a" * 99 + "\n") * 81 + "a" * 92 + "\n"),
                         ["src/AGENTS.md: 8193 bytes, over the 8192-byte budget"])

    def test_a_line_over_300_characters_fails(self):
        self.assertEqual(project_n.doc_budget_problems("tests/AGENTS.md", "x\n" + "b" * 301 + "\n"),
                         ["tests/AGENTS.md:2: 301 characters, over the 300-character line budget"])

    def test_a_line_of_300_characters_passes(self):
        self.assertEqual(project_n.doc_budget_problems("tests/AGENTS.md", "b" * 300 + "\n"), [])

    def test_a_skills_reference_file_has_no_line_budget(self):
        self.assertEqual(project_n.doc_budget_problems(".claude/skills/x/references/r.md", "l\n" * 201), [])

    def test_a_line_is_measured_in_characters(self):
        self.assertEqual(project_n.doc_budget_problems("tools/AGENTS.md", "\u2192" * 300 + "\n"), [])  # 900 bytes

    def test_the_byte_budget_counts_bytes_not_characters(self):
        text = ("\u2192" * 100 + "\n") * 28  # 2,828 characters, 8,428 bytes
        self.assertEqual(project_n.doc_budget_problems("src/AGENTS.md", text),
                         ["src/AGENTS.md: 8428 bytes, over the 8192-byte budget"])

    def test_an_agent_brief_at_60_lines_passes(self):
        self.assertEqual(project_n.doc_budget_problems(".claude/agents/x.md", "l\n" * 60), [])

    def test_other_docs_have_no_budget(self):
        for path in ("README.md", "src/CLAUDE.md", "docs/NOTAGENTS.md"):
            self.assertEqual(project_n.doc_budget_problems(path, "f" * 20000 + "\n"), [], path)

    def test_a_claude_md_pointing_at_its_agents_md_passes(self):
        self.assertEqual(project_n.claude_md_problems("src/CLAUDE.md", "@AGENTS.md\n"), [])
        self.assertEqual(project_n.claude_md_problems(".claude/CLAUDE.md", "@../AGENTS.md\n"), [])

    def test_a_claude_md_holding_more_fails(self):
        self.assertEqual(project_n.claude_md_problems("src/CLAUDE.md", "@AGENTS.md\nmore\n"),
                         ["src/CLAUDE.md: must hold only '@AGENTS.md'"])
        self.assertEqual(len(project_n.claude_md_problems(".claude/CLAUDE.md", "@AGENTS.md\n")), 1)

    def test_only_claude_md_files_are_checked_for_the_pointer(self):
        self.assertEqual(project_n.claude_md_problems("README.md", "anything"), [])
        self.assertEqual(project_n.claude_md_problems("docs/NOTCLAUDE.md", "anything"), [])

    def test_every_claude_md_is_a_pointer(self):
        self.assertEqual(project_n.misshaped_claude_docs(), [])

    def test_the_root_agents_md_is_exempt(self):
        self.assertEqual(project_n.doc_budget_problems("AGENTS.md", "c" * 20000 + "\n"), [])

    def test_a_skill_over_200_lines_fails(self):
        self.assertEqual(project_n.doc_budget_problems(".claude/skills/x/SKILL.md", "l\n" * 201),
                         [".claude/skills/x/SKILL.md: 201 lines, over the 200-line budget"])

    def test_a_skill_at_200_lines_passes_with_long_lines(self):
        self.assertEqual(project_n.doc_budget_problems(".claude/skills/x/SKILL.md", ("d" * 400 + "\n") * 200), [])

    def test_an_agent_brief_over_60_lines_fails(self):
        self.assertEqual(project_n.doc_budget_problems(".claude/agents/x.md", "l\n" * 61),
                         [".claude/agents/x.md: 61 lines, over the 60-line budget"])

    def test_a_nested_agents_md_is_held_to_the_budget(self):
        self.assertEqual(len(project_n.doc_budget_problems("src/game/AGENTS.md", "e" * 301 + "\n")), 1)

    def test_agent_docs_fit_their_budgets(self):
        self.assertEqual(project_n.oversized_agent_docs(), [])


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


REFERENCES_FOUND = {
    "a phase and chunk": "// (the phase 6 chunk 4 spec)",
    "a design doc section": "/// see design doc 0002 \u00a77.6",
    "a design doc number alone": "// as 0002.3 does",
    "a section written S": "// the 0002 S11 order",
    "a chunk": "// chunk 4b",
    "a phase in Python": "# phase 5's gate",
    "a ruling": "// ruling R6a",
    "a task": "// Task 7's test",
    "a chunk name": "// 5b-look restyles it",
    "a wave": "// wave C adds the rest",
    "a date": "// measured 2026-11-15",
    "the plan": "// per the plan, this goes first",
    "the user's pointer": "int x = 0;  // the user's tuned value",
    "a block comment": "/* chunk 5 */ int x = 0;",
    "a later line of a block comment": "/*\n * why this order\n * (phase 4)\n */\nint x = 0;",
    "a user's call": "// the user's call: keep the old order",
    "a user's cold rule": "# the user's cold rule",
    "chunk names in a list": "// 5b, 5c share it",
    "a Python docstring": '"""Warm on branches (the user\'s cold rule)."""',
}
REFERENCES_NOT_FOUND = {
    "code, not a comment": "int phase = 0;",
    "a string literal": 'const char* const NAME = "chunk 4";',
    "a string holding a comment mark": 'const char* const NAME = "// chunk 4";',
    "a word without a digit": "// the third phase of the fight",
    "a data chunk": "// one chunk of the file at a time",
    "a suppression": "int x = 0;  // NOLINT(readability-magic-numbers): the C API's value",
    "a texture format": "// an R8 texture holds the mask, in display P3",
    "a hardware spec": "// the minimum spec's core count",
    "a hash name": "// FNV-1a 64 of the bytes",
    "a design word": "// the design of the queue holds",
    "a small decimal": "// tolerance 0.0001 of a pixel",
    "a version string": "// since version 1.0002 of the format",
    "a ruler": "// the ruler's tick marks the edge",
    "a task without a number": "// a background task queue",
    "a wave of things": "// a wave of enemies",
    "a plan-like word": "// the planner's output",
    "a year alone": "// 2026 in the header",
    "a compiler flag": "// -O2 and -O0 builds differ here",
    "a link name": "// an 8b/10b link",
    "the user's setting": "// the user's settings.yaml wins",
    "the user's chosen option": "// the user's chosen key bindings",
    "dice": "// roll (d6) dice",
    "a sensor": "// an (O2) sensor reads it",
    "a standard's section spelled out": "// RFC 8259 section 7 escapes it",
    "a hex literal": "// 0x5b marks it",
    "the user as a role": "// The user's own sanitizer options go last",
    "a user to check": "# for the user to check and publish",
    "a technique's user": "// its user's death ends it",
}


class DesignReferenceTest(unittest.TestCase):
    """The comment check: code comments say why in their own words, never citing the design process."""

    def test_each_reference_is_found(self):
        for name, text in REFERENCES_FOUND.items():
            with self.subTest(name):
                path = "tools/a.py" if text.startswith(("#", '"""')) else "src/a.cpp"
                self.assertTrue(project_n.design_references(path, text + "\n"), text)

    def test_ordinary_text_is_not_found(self):
        for name, text in REFERENCES_NOT_FOUND.items():
            with self.subTest(name):
                path = "tools/a.py" if text.startswith("#") else "src/a.cpp"
                self.assertEqual(project_n.design_references(path, text + "\n"), [], text)

    def test_a_python_string_is_not_a_comment(self):
        self.assertEqual(project_n.design_references("tools/a.py", 'x = "# chunk 4"\n'), [])

    def test_documentation_is_not_code(self):
        self.assertEqual(project_n.design_reference_files(["docs/a.md", "AGENTS.md", "src/core/generated/atlas.hpp",
                                                           "assets/manifest.yaml", "tests/fuzz/corpus/yaml/world",
                                                           "src/core/a.cpp", "xmake.lua", "shaders/a.hlsl", ".vscode/settings.json",
                                                           "tests/reference/reference_tests.cpp", "tests/reference/a.png",
                                                           "dev", "dev.cmd"]),
                         ["src/core/a.cpp", "xmake.lua", "shaders/a.hlsl", ".vscode/settings.json",
                          "tests/reference/reference_tests.cpp", "dev", "dev.cmd"])

    def test_a_finding_names_the_line_and_the_rule(self):
        found = project_n.design_references("src/a.cpp", "int a = 0;\n// chunk 4b\n")
        self.assertEqual(len(found), 1)
        self.assertTrue(found[0].startswith("src/a.cpp:2: "))
        self.assertIn("own words", found[0])

    def test_a_batch_script_comment(self):
        self.assertEqual(len(project_n.design_references("dev.cmd", "@rem phase 6 shorthand\n:: chunk 4\n@echo off\n")), 2)
        self.assertEqual(project_n.design_references("dev.cmd", "@echo the phase 6 value\n"), [])

    def test_a_shell_script_comment(self):
        self.assertEqual(len(project_n.design_references("dev", "#!/bin/sh\n# phase 6 shorthand\n")), 1)

    def test_an_apostrophe_in_a_yaml_value_is_no_quote(self):
        self.assertEqual(len(project_n.design_references("assets/data/a.yaml", "name: Wanderer's rest  # phase 4\n")), 1)

    def test_a_yaml_escaped_quote_stays_in_the_string(self):
        self.assertEqual(project_n.design_references("assets/data/a.yaml", "k: 'it''s # phase 4'\n"), [])
        self.assertEqual(len(project_n.design_references("assets/data/a.yaml", "k: 'it''s'  # phase 4\n")), 1)

    def test_a_quoted_yaml_list_item_hides_its_hash(self):
        self.assertEqual(project_n.design_references("assets/data/a.yaml", "- 'a # chunk 4'\nk: [\"b # chunk 4\"]\n"), [])

    def test_a_yaml_comment_after_a_value(self):
        self.assertTrue(project_n.design_references("assets/data/a.yaml", "key: 1  # phase 6 value\n"))
        self.assertEqual(project_n.design_references("assets/data/a.yaml", "key: 'a # chunk 4'\n"), [])


class SanitizerTest(unittest.TestCase):
    def test_each_sanitizer_gets_its_own_build_dir(self):
        self.assertEqual(project_n.build_dir("linux-gcc", "debug").name, "debug")
        self.assertEqual(project_n.build_dir("linux-gcc", "debug", "address").name, "debug-address")

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

    def test_doctest_case_count_reads_the_listings_last_line(self):
        listing = "[doctest] listing all test case names\n====\nparse\n====\n[doctest] unskipped test cases passing the current filters: 1\n"
        self.assertEqual(project_n.doctest_case_count(listing), 1)
        self.assertEqual(project_n.doctest_case_count(listing.replace(": 1", ": 0")), 0)
        with self.assertRaises(project_n.Fail) as raised:  # a broken run, not an empty selection
            project_n.doctest_case_count("odd output")
        self.assertIn("odd output", str(raised.exception))

    def test_doctest_case_names_skip_the_listings_frame(self):
        listing = "[doctest] listing all test case names\n====\nparse a path\n  parse b\n====\n[doctest] unskipped: 2\n"
        self.assertEqual(project_n.doctest_case_names(listing), ["parse a path", "parse b"])

    def test_candidates_are_the_closest_files_and_cases(self):
        near = project_n.test_candidates("route_graph_tests", ["route_graph", "routes", "log"], ["a route graph case", "zzz"])
        self.assertIn("route_graph", near)
        self.assertNotIn("zzz", near)
        self.assertEqual(project_n.test_candidates("qqqq", ["log"], ["zzz"]), [])

    @staticmethod
    def run_listing(counts, names="[doctest] x\n====\nparse path\n====\n", args=(), listed=None):
        """cmd_test with --test and doctest's listing mocked: the one with the `--test-case` filter prints `counts`, the
        other `names`; `listed` collects each listing's arguments."""
        def listing(target, bdir, filters=()):
            if listed is not None:
                listed.append(list(filters))
            filtered = any(f.startswith("--test-case=") for f in filters)
            return (f"[doctest] unskipped test cases passing the current filters: {counts}\n" if filtered else names)
        ran = []
        with mock.patch.multiple(project_n, build=lambda *a: Path("build"), xmake_env=lambda bdir: {},
                                 needs_container=lambda *a: False, run=lambda argv, env: ran.append(argv),
                                 list_test_cases=listing):
            project_n.cmd_test(project_n.argparse.Namespace(preset=project_n.NATIVE_PRESET, mode="debug", sanitizer="none",
                                                            container=False, target="core_tests", test="nothing_like_it", args=list(args)))
        return ran

    def test_a_test_matching_no_file_and_no_case_is_refused_naming_candidates(self):
        with self.assertRaises(project_n.Fail) as raised:
            self.run_listing(0, "[doctest] x\n====\nnothing like that\nzzz unrelated\n====\n")
        self.assertIn("did you mean: nothing like that", str(raised.exception))
        self.assertNotIn("zzz unrelated", str(raised.exception))

    def test_a_skipped_case_run_with_no_skip_is_listed_with_it_and_runs(self):
        listed = []
        ran = self.run_listing(1, args=["--", "--no-skip"], listed=listed)
        self.assertEqual(listed, [["--test-case=*nothing_like_it*", "--no-skip"]])
        self.assertEqual(ran, [["xmake", "run", "core_tests", "--test-case=*nothing_like_it*", "--no-skip"]])

    def test_the_candidate_listing_keeps_no_skip_but_not_the_filter(self):
        listed = []
        with self.assertRaises(project_n.Fail):
            self.run_listing(0, args=["--no-skip"], listed=listed)
        self.assertEqual(listed[1], ["--no-skip"])

    def test_a_listing_without_a_count_is_refused_as_broken(self):
        with self.assertRaises(project_n.Fail) as raised:
            self.run_listing("?", names="x")  # the mock's count line never matches
        self.assertNotIn("matches no test file", str(raised.exception))

    def test_a_test_matching_no_file_and_no_case_with_no_candidates_is_refused(self):
        with self.assertRaises(project_n.Fail) as raised:
            self.run_listing(0)
        self.assertNotIn("did you mean", str(raised.exception))

    def test_a_test_that_is_a_case_substring_still_runs(self):
        self.assertEqual(self.run_listing(1), [["xmake", "run", "core_tests", "--test-case=*nothing_like_it*"]])

    def test_list_test_cases_runs_the_listing_through_the_runner(self):
        calls = []
        done = mock.Mock(stdout="out")
        with mock.patch.multiple(project_n, xmake_env=lambda bdir: {"E": "1"}, run=lambda argv, env, **kw: calls.append((argv, env)) or done):
            self.assertEqual(project_n.list_test_cases("core_tests", Path("b"), ["--test-case=*x*"]), "out")
        self.assertEqual(calls, [(["xmake", "run", "core_tests", "--list-test-cases", "--test-case=*x*"], {"E": "1"})])

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
                                                            target=None, test="data_watch", args=[]))

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
    def ran(target, test=None, args=(), preset=project_n.NATIVE_PRESET, sanitizer="none"):
        ran = []
        with mock.patch.multiple(project_n, build=lambda *a: Path("build"), xmake_env=lambda bdir: {},
                                 needs_container=lambda *a: False, run=lambda argv, env: ran.append(argv),
                                 list_test_cases=lambda *a, **kw: "[doctest] unskipped test cases passing the current filters: 1"):
            project_n.cmd_test(project_n.argparse.Namespace(preset=preset, mode="debug", sanitizer=sanitizer,
                                                            container=False, target=target, test=test, args=list(args)))
        return ran

    def test_a_suite_without_filters_runs_its_file_tests(self):
        self.assertEqual(self.ran("game_tests"), [["xmake", "test", "--output-on-failure", "game_tests/*"]])

    def test_a_suite_with_filters_runs_its_binary(self):
        self.assertEqual(self.ran("game_tests", test="save"),
                         [["xmake", "run", "game_tests", project_n.test_filter("save", "game_tests")]])
        self.assertEqual(self.ran("game_tests", args=["--", "--count"]), [["xmake", "run", "game_tests", "--count"]])

    def test_a_target_that_is_no_suite_runs_its_binary(self):  # the benchmark registers no test
        self.assertEqual(self.ran("headless_bench"), [["xmake", "run", "headless_bench"]])

    def test_a_fuzz_suite_replays_its_seeds(self):
        self.assertEqual(self.ran("fuzz_yaml", preset="linux-clang", sanitizer="address"),
                         [["xmake", "test", "--output-on-failure", "fuzz_yaml/*"]])

    def test_a_fuzz_suite_elsewhere_is_refused_with_the_command(self):  # only that build makes the fuzz targets
        for preset, sanitizer in (("linux-clang", "none"), ("linux-gcc", "address"), ("linux-clang", "thread")):
            with self.assertRaisesRegex(project_n.Fail, r"--preset linux-clang --sanitizer address"):
                self.ran("fuzz_yaml", preset=preset, sanitizer=sanitizer)

    def test_a_fuzz_suite_takes_no_test_or_doctest_arguments(self):
        with self.assertRaisesRegex(project_n.Fail, "fuzz_save"):
            self.ran("fuzz_save", test="save", preset="linux-clang", sanitizer="address")
        with self.assertRaisesRegex(project_n.Fail, "fuzz_save"):
            self.ran("fuzz_save", args=["--", "-runs=10"], preset="linux-clang", sanitizer="address")

    def test_a_name_that_is_no_suite_is_refused(self):  # `--suite core` ran the core library and passed
        for name in ("core", "game", "fuzz_nothing"):
            with self.assertRaises(project_n.Fail) as refused:
                self.ran(name)
            for valid in ("core_tests", "game_tests", "headless_bench", "reference_tests", "fuzz_yaml"):
                self.assertIn(valid, str(refused.exception), name)

    def test_the_game_is_pointed_at_run(self):
        with self.assertRaisesRegex(project_n.Fail, r"project_n run"):
            self.ran("project_n")

    def test_a_program_takes_no_test(self):
        with self.assertRaisesRegex(project_n.Fail, "headless_bench is no doctest suite"):
            self.ran("headless_bench", test="physics")  # a case name, not a file: test_filter takes it

    def test_a_program_keeps_its_arguments(self):
        self.assertEqual(self.ran("headless_bench", args=["--", "--ticks", "10"]),
                         [["xmake", "run", "headless_bench", "--ticks", "10"]])

    def test_a_refused_name_never_reaches_a_container(self):
        with mock.patch.multiple(project_n, needs_container=lambda *a: True, reenter=mock.DEFAULT,
                                 build=mock.DEFAULT) as patched, self.assertRaises(project_n.Fail):
            project_n.cmd_test(project_n.argparse.Namespace(preset="linux-clang", mode="debug", sanitizer="none",
                                                            container=True, target="core", test=None, args=[]))
        patched["reenter"].assert_not_called()
        patched["build"].assert_not_called()

    def test_a_fuzz_suite_reaches_its_container_with_its_target(self):
        with mock.patch.multiple(project_n, needs_container=lambda *a: True, reenter=mock.DEFAULT) as patched:
            project_n.cmd_test(project_n.argparse.Namespace(preset="linux-clang", mode="debug", sanitizer="address",
                                                            container=True, target="fuzz_yaml", test=None, args=[]))
        patched["reenter"].assert_called_once_with("linux-clang", ["test", "--preset", "linux-clang", "--mode", "debug",
                                                                   "--sanitizer", "address", "--target", "fuzz_yaml"])

    def test_a_test_reaches_its_container_as_a_name_so_the_container_checks_it(self):
        with mock.patch.multiple(project_n, needs_container=lambda *a: True, reenter=mock.DEFAULT) as patched:
            project_n.cmd_test(project_n.argparse.Namespace(preset="windows-msvc", mode="debug", sanitizer="none", container=False,
                                                            target="core_tests", test="route", args=["--", "--no-skip"]))
        patched["reenter"].assert_called_once_with("windows-msvc", ["test", "--preset", "windows-msvc", "--mode", "debug",
                                                                    "--sanitizer", "none", "--target", "core_tests", "--test", "route",
                                                                    "--", "--no-skip"])

    def test_a_fuzz_refusal_keeps_the_mode_and_container_asked_for(self):
        with mock.patch.multiple(project_n, needs_container=lambda *a: False, build=mock.DEFAULT), \
             self.assertRaisesRegex(project_n.Fail, r"--mode release --container --suite fuzz_yaml"):
            project_n.cmd_test(project_n.argparse.Namespace(preset="linux-clang", mode="release", sanitizer="none",
                                                            container=True, target="fuzz_yaml", test=None, args=[]))

    def test_listing_a_fuzz_suite_prints_nothing(self):  # it has no test files for --test
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            project_n.cmd_test(project_n.argparse.Namespace(list_tests=True, target="fuzz_yaml"))
        self.assertEqual(out.getvalue().strip(), "")

    def test_every_fuzz_file_and_program_is_a_known_name(self):
        # Every fuzz target keeps its seeds in a corpus folder of its own name (tests/AGENTS.md).
        corpora = (project_n.ROOT / "tests" / "fuzz" / "corpus").iterdir()
        self.assertEqual(project_n.fuzz_suites(), sorted("fuzz_" + d.name for d in corpora if d.is_dir()))
        self.assertIn("headless_bench", project_n.test_programs())
        self.assertIn("reference_tests", project_n.test_programs())
        self.assertNotIn("test_pch", project_n.test_programs())
        self.assertTrue(set(project_n.test_programs()).isdisjoint(project_n.test_names()))


class EmptyOutputsTest(unittest.TestCase):
    def test_only_empty_regular_files_directly_in_the_folder_go(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp) / "bin"
            (folder / "sub").mkdir(parents=True)
            (folder / "empty_dir").mkdir()
            (folder / "b_tests").write_bytes(b"")
            (folder / "a_tests").write_bytes(b"")
            (folder / "full").write_bytes(b"x")
            (folder / "sub" / "nested").write_bytes(b"")
            (folder / "sub" / "kept").write_bytes(b"x")
            (Path(tmp) / "outside").write_bytes(b"")
            symlinks = True
            try:
                (folder / "link_to_empty").symlink_to(Path(tmp) / "outside")
                (folder / "dangling").symlink_to(Path(tmp) / "missing")
            except OSError:  # no symlink right on this host
                symlinks = False
            self.assertEqual(project_n.remove_empty_outputs(folder), ["a_tests", "b_tests"])
            self.assertEqual(sorted(p.name for p in folder.iterdir()),
                             sorted(["sub", "empty_dir", "full", *(["link_to_empty", "dangling"] if symlinks else [])]))
            self.assertTrue((folder / "sub" / "nested").is_file())  # a subfolder's files stay
            self.assertTrue((Path(tmp) / "outside").is_file())  # nothing a link points at

    def test_a_missing_folder_or_a_clean_one_removes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(project_n.remove_empty_outputs(Path(tmp) / "none"), [])
            self.assertEqual(project_n.remove_empty_outputs(Path(tmp)), [])

    def test_build_clears_the_binary_folder_before_xmake_and_says_so(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / "app").write_bytes(b"")
            out, order = io.StringIO(), []
            with mock.patch.multiple(project_n, configure=lambda *a: Path("bdir"), xmake_env=lambda bdir: {},
                                     binary_path=lambda *a, **k: folder / "app", IN_CONTAINER=True,
                                     run=lambda argv, env=None: order.append(((folder / "app").exists(), argv))), \
                 contextlib.redirect_stdout(out):
                project_n.build("linux-gcc", "release")
            self.assertEqual(order, [(False, ["xmake", "build"])])
            self.assertEqual(out.getvalue(), "build: removed 1 empty outputs a stopped build left: app\n")
            out = io.StringIO()
            with mock.patch.multiple(project_n, configure=lambda *a: Path("bdir"), xmake_env=lambda bdir: {},
                                     binary_path=lambda *a, **k: folder / "app", IN_CONTAINER=True, run=lambda argv, env=None: None), \
                 contextlib.redirect_stdout(out):
                project_n.build("linux-gcc", "release")
            self.assertEqual(out.getvalue(), "")


class SanitizerBuildFolderTest(unittest.TestCase):
    def test_a_sanitizer_build_clears_its_own_tree_and_leaves_the_plain_one(self):
        preset = "linux-gcc"
        plat = project_n.PRESETS[preset]
        cwd = os.getcwd()
        with tempfile.TemporaryDirectory() as tmp:
            os.chdir(tmp)  # the build folders are relative to the working directory
            try:
                folders = {s: project_n.build_dir(preset, "debug", s) / plat["plat"] / plat["arch"] / "debug"
                           for s in ("none", "address")}
                for folder in folders.values():
                    folder.mkdir(parents=True)
                    (folder / project_n.APP).write_bytes(b"")
                with mock.patch.multiple(project_n, configure=lambda *a: Path("bdir"), xmake_env=lambda bdir: {},
                                         run=lambda argv, env=None: None), contextlib.redirect_stdout(io.StringIO()):
                    project_n.build(preset, "debug", sanitizer="address")
                self.assertNotEqual(folders["none"], folders["address"])
                self.assertFalse((folders["address"] / project_n.APP).exists())
                self.assertTrue((folders["none"] / project_n.APP).exists())
            finally:
                os.chdir(cwd)


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
        ran = []
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "lib").mkdir()
            (root / "lib" / "libc++.1.dylib").write_bytes(b"dylib")
            (root / "project_n").write_bytes(b"binary")
            with (mock.patch.dict(project_n.os.environ, {"CONDA_PREFIX": d, "MACOSX_DEPLOYMENT_TARGET": "13.0"}),
                  mock.patch.object(project_n, "run", lambda cmd, **_: ran.append((cmd[0], Path(cmd[-1]).name)))):
                project_n.macos_bundle(root / "dist", root / "project_n")
            contents = root / "dist" / "project_n.app" / "Contents"
            self.assertEqual((contents / "MacOS" / "project_n").read_bytes(), b"binary")
            self.assertTrue((contents / "Frameworks" / "libc++.1.dylib").is_file())
            plist = project_n.plistlib.loads((contents / "Info.plist").read_bytes())
        self.assertEqual((plist["CFBundleExecutable"], plist["LSMinimumSystemVersion"]), ("project_n", "13.0"))
        # The rpath goes before signing (a signature install_name_tool broke won't run), nested code is signed first.
        self.assertEqual(ran, [("install_name_tool", "project_n"), ("codesign", "libc++.1.dylib"),
                               ("codesign", "project_n.app"), ("codesign", "project_n.app")])

    def test_a_repeated_macos_artefact_reusing_its_build_ships_without_the_rpath(self):
        # With --reuse-build XMake doesn't relink, so an rpath deleted from the build folder's binary is gone the next
        # time and install_name_tool fails ("no LC_RPATH load command"). The fake keeps the rpath in the file's bytes.
        def run(cmd, **_):
            if cmd[0] == "install_name_tool":
                target = root / cmd[-1]  # run's working directory is ROOT
                if b"rpath" not in target.read_bytes():
                    raise subprocess.CalledProcessError(1, cmd)
                target.write_bytes(target.read_bytes().replace(b"rpath", b""))

        answers = {"otool": "project_n:\n\t/usr/lib/libSystem.B.dylib\n", "git": "abc\n", "xcrun": "15.0\n"}
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            with mock.patch.object(project_n, "IN_CONTAINER", True):
                binary = project_n.binary_path("macos-clang", "release")
            (root / binary).parent.mkdir(parents=True)
            (root / binary).write_bytes(b"binary rpath")
            (root / binary.with_suffix(".dSYM")).mkdir()
            (root / "lib").mkdir()
            (root / "lib" / "libc++.1.dylib").write_bytes(b"dylib")
            (root / "assets").mkdir()
            for name, text in (("LICENSE", "AGPL"), ("THIRD_PARTY_NOTICES.md", "notices"), ("pixi.lock", "lock"),
                               ("xmake.lua", 'set_version("0.1.0")')):
                (root / name).write_text(text)
            env = {k: v for k, v in os.environ.items() if k not in ("GITHUB_OUTPUT", "GITHUB_REF")}
            env |= {"CONDA_PREFIX": d, "MACOSX_DEPLOYMENT_TARGET": "13.0", "GITHUB_REF": "refs/heads/ci-speed"}
            with (mock.patch.dict(project_n.os.environ, env, clear=True),
                  mock.patch.multiple(project_n, ROOT=root, IN_CONTAINER=True, tree_dirty=lambda: "", run=run,
                                      build=lambda *args: binary.parents[3], xmake_env=lambda bdir: {},
                                      capture=lambda cmd, **_: answers[cmd[0]]),
                  contextlib.redirect_stdout(io.StringIO())):
                for _ in range(2):
                    project_n.cmd_artifact(project_n.argparse.Namespace(preset="macos-clang", reuse_build=True))
            shipped = root / "dist" / "macos-clang" / "project_n.app" / "Contents" / "MacOS" / "project_n"
            self.assertEqual(shipped.read_bytes(), b"binary ")

    def test_msvc_install_follows_only_its_containerfile(self):
        self.assertRegex(project_n.msvc_dir().name, r"^msvc-[0-9a-f]{12}$")

    def test_container_command_mounts_repo_and_cache(self):
        with mock.patch.dict(project_n.os.environ):
            project_n.os.environ.pop("PROJECT_N_XMAKE_CACHE", None)  # CI sets it
            cmd = project_n.container_cmd("img", ["pixi", "run", "project_n", "build"])
        self.assertIn(f"{project_n.ROOT}:/src:Z", cmd)
        self.assertIn(f"{project_n.CACHE_VOLUME}:/home/dev/.xmake", cmd)
        self.assertEqual(cmd[-5:], ["img", "pixi", "run", "project_n", "build"])

    def test_a_relative_package_cache_folder_is_made_absolute(self):
        # podman would take a relative source for a named volume, so the packages would miss the folder CI caches.
        with mock.patch.dict(project_n.os.environ, {"PROJECT_N_XMAKE_CACHE": "rel-xmake"}), \
             mock.patch.object(project_n.Path, "mkdir"):
            cmd = project_n.container_cmd("img", ["true"])
        self.assertIn(f"{Path.cwd() / 'rel-xmake'}:/home/dev/.xmake:Z", cmd)


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
    def assert_gutters_hold(self, sizes, places, page, gutter):
        rects = [(p, x, y, x + w, y + h) for (p, x, y), (w, h) in zip(places, sizes)]
        for p, x0, y0, x1, y1 in rects:
            self.assertTrue(x0 >= gutter and y0 >= gutter and x1 <= page - gutter and y1 <= page - gutter, (x0, y0, x1, y1))
        for i, a in enumerate(rects):
            for b in rects[i + 1:]:
                apart = (a[0] != b[0] or a[3] + gutter <= b[1] or b[3] + gutter <= a[1] or a[4] + gutter <= b[2]
                         or b[4] + gutter <= a[2])
                self.assertTrue(apart, (a, b))

    def test_sprites_keep_their_gutters_and_never_overlap(self):
        sizes = [(100, 40), (30, 60), (90, 50), (60, 60), (20, 10)]
        self.assert_gutters_hold(sizes, project_n.pack_atlas(sizes, page=200, gutter=8, align=1), 200, 8)

    def test_every_region_starts_on_the_last_mips_texel(self):
        """So each mip level's texels fall on a sprite the same way wherever it lands, and a re-pack draws it as before."""
        sizes = [(1920, 1080), (256, 256), (180, 128), (92, 68), (33, 17), (10, 40), (48, 64), (8, 8)]
        places = project_n.pack_atlas(sizes)
        align = 1 << (project_n.ATLAS_MIPS - 1)
        for (_, x, y), size in zip(places, sizes):
            self.assertEqual((x % align, y % align), (0, 0), (size, x, y))
        self.assert_gutters_hold(sizes, places, project_n.ATLAS_PAGE, project_n.ATLAS_GUTTER)

    def test_appending_a_sprite_moves_no_earlier_one(self):
        """Sprites are appended to art/sprites.toml, so the packer places them in list order: an earlier one never
        moves, and its pixels draw as before. The appended ones: a taller one than the open shelf, a small one and one
        that needs a new page."""
        sizes = [(1920, 1080), (180, 128), (92, 68), (240, 380), (256, 256), (48, 64)]
        places = project_n.pack_atlas(sizes)
        for added in ([(300, 420)], [(300, 420), (8, 8)], [(300, 420), (8, 8), (1900, 1900)]):
            grown = project_n.pack_atlas(sizes + added)
            self.assertEqual(grown[:len(sizes)], places, added)
            self.assert_gutters_hold(sizes + added, grown, project_n.ATLAS_PAGE, project_n.ATLAS_GUTTER)

    def test_the_packer_keeps_a_32_texel_gutter(self):
        (_, x0, y0), (_, x1, y1) = project_n.pack_atlas([(10, 40), (24, 48)])
        self.assertTrue(min(x0, y0, x1, y1) >= 32, (x0, y0, x1, y1))
        first, second = sorted([(x0, 10), (x1, 24)])  # left to right
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


@functools.cache
def placeholders():
    """art/placeholders.py, loaded from its path (art/ isn't on the self-tests' import path) and registered as a module,
    so its types pickle and dataclasses resolve as an imported module's would."""
    spec = importlib.util.spec_from_file_location("placeholders", Path(__file__).resolve().parent.parent / "art" / "placeholders.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def clear_room(name):
    """Paints placeholder `name` and returns how many texel columns at its left and right, and rows at its top and
    bottom, hold no art: alpha that its PNG rounds to 0. Runs in a worker process: painting every stamp, mist and
    painting one after another is slow."""
    painter, _ = placeholders().PLACEHOLDERS[name]
    canvas = painter()
    width, height = canvas.width, canvas.height
    art = [[canvas.a[y * width + x] * 255.0 + 0.5 >= 1.0 for x in range(width)] for y in range(height)]
    columns = [x for x in range(width) if any(row[x] for row in art)]
    rows = [y for y in range(height) if any(art[y])]
    if not columns:
        return width, width, height, height
    return columns[0], width - 1 - columns[-1], rows[0], height - 1 - rows[-1]


class PlaceholderTest(unittest.TestCase):
    maxDiff = None  # a failure lists every mark cut, each on its own line

    def test_painted_marks_keep_their_art_off_their_edges(self):
        """At the far zoom a mark's art is drawn from small mips, and the sprite's quad cutting into their blur leaves a
        hard edge that crawls as the camera moves, so each kind of mark keeps clear texels at its left and right, top
        and bottom. Stamps keep the placeholders' STAMP_MARGIN at the sides and top, and paintings their WASH_ROOM under
        them; the rest is what each kind keeps now: a stamp stands on its sheet's bottom edge, its foot, so room below
        would float it above its tile, and mist and paintings are drawn larger and softer."""
        module = placeholders()
        margins = {"stamp_": (module.STAMP_MARGIN, module.STAMP_MARGIN, 1), "mist_": (2, 2, 2),
                   "painting_": (9, 19, module.WASH_ROOM)}
        needed = {name: margin for name in module.PLACEHOLDERS for prefix, margin in margins.items()
                  if name.startswith(prefix)}
        self.assertTrue(all(any(name.startswith(prefix) for name in needed) for prefix in margins))
        names = list(needed)
        with concurrent.futures.ProcessPoolExecutor(max_workers=min(len(names), os.cpu_count() or 1)) as pool:
            rooms = dict(zip(names, pool.map(clear_room, names)))
        cut = [f"{name}: {left} clear at the left, {right} at the right, {top} at the top, {bottom} at the bottom; "
               f"{needed[name][0]}, {needed[name][1]} and {needed[name][2]} needed"
               for name, (left, right, top, bottom) in rooms.items()
               if min(left, right) < needed[name][0] or top < needed[name][1] or bottom < needed[name][2]]
        self.assertEqual(cut, [])

    def test_a_forest_that_cannot_clear_its_top_margin_is_refused_by_name(self):
        module = placeholders()
        width, height = module.STAMPS["forest"]
        with mock.patch.object(module, "STAMP_MARGIN", width + height), \
                self.assertRaisesRegex(ValueError, "forest variant 0"):
            module.forest(width, height, 0, 1)


@functools.cache
def world_map():
    """art/world_map.py, loaded from its path after placeholders.py, whose noise it imports."""
    placeholders()
    spec = importlib.util.spec_from_file_location("world_map", Path(__file__).resolve().parent.parent / "art" / "world_map.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class WorldMapRoadsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = world_map()
        cls.text = (Path(__file__).resolve().parent.parent / "assets" / "data" / "world.yaml").read_text(encoding="utf-8")
        cls.world = cls.module.read_world(cls.text)
        cls.links, cls.fords = cls.module.world_links(cls.world)
        cls.roads = cls.module.all_roads(cls.links)
        cls.out = cls.module.rewrite_roads(cls.text, cls.roads, cls.fords)
        wary = copy.deepcopy(cls.world)
        wary["danger_reach"] = 6
        wary["profiles"] = [p for p in wary["profiles"] if p["key"] != "on_foot_wary"]
        wary["profiles"].append({"key": "on_foot_wary", "ticks_per_cost": 16, "features": {"danger": 300}})
        wary["travel"]["bands"] = [{"from_power": 0, "profile": "on_foot_wary"}, {"from_power": 5, "profile": "on_foot"}]
        cls.wary = wary
        cls.wary_roads, cls.wary_fords = cls.module.route_roads(wary)

    def test_only_the_roads_and_fords_blocks_change(self):
        def without_blocks(text):
            return re.sub(r"(?m)^(roads|fords):\n(?:  .*\n)*", r"\1:\n", text)
        self.assertEqual(without_blocks(self.out), without_blocks(self.text))
        self.assertIn("# Each a polyline of [column, row] tile points; a road over a river is a bridge.\nroads:\n", self.out)

    def test_the_shipped_roads_are_the_ones_the_file_routes(self):
        self.assertEqual(self.out, self.text,
                         "world.yaml's roads or fords are stale: run `pixi run python art/world_map.py --roads`")

    def test_a_generated_world_holds_the_danger_travel_and_its_roads_are_a_roads_pass_over_it(self):
        module = self.module
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder)
            saved = (module.YAML_OUT, module.PREVIEW, module.TERRAIN_PREVIEW, module.ROOT, sys.argv)
            module.ROOT = target
            module.YAML_OUT, module.PREVIEW, module.TERRAIN_PREVIEW = (
                target / "world.yaml", target / "preview.png", target / "terrain.png")
            sys.argv = ["world_map.py"]
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    module.main()
                text = module.YAML_OUT.read_text(encoding="utf-8")
            finally:
                module.YAML_OUT, module.PREVIEW, module.TERRAIN_PREVIEW, module.ROOT, sys.argv = saved
        world = module.read_world(text)
        self.assertEqual(world["danger_reach"], self.world["danger_reach"])
        self.assertEqual(world["profiles"], self.world["profiles"])
        self.assertEqual(world["travel"]["bands"], self.world["travel"]["bands"])
        self.assertGreater(module.wary_danger_cost(world), 0)
        self.assertIn("`art/world_map.py --roads` writes", text)
        self.assertEqual(module.rewrite_roads(text, *module.route_roads(world)), text)

    def test_a_second_pass_changes_nothing(self):
        again = self.module.rewrite_roads(self.out, *self.module.route_roads(self.module.read_world(self.out)))
        self.assertEqual(again, self.out)

    def test_a_comment_inside_the_roads_block_is_refused_with_its_line(self):
        lines = self.text.split("\n")
        at = lines.index("roads:") + 2
        lines.insert(at, "  # a hand note")
        with self.assertRaisesRegex(SystemExit, f"line {at + 1} "):
            self.module.rewrite_roads("\n".join(lines), self.roads, self.fords)

    def test_every_city_is_reachable_with_and_without_the_wary_cost(self):
        # The danger half guards the rule that a danger cost never turns ground impassable.
        module = self.module
        for world, roads, fords in ((self.world, self.roads, self.fords), (self.wary, self.wary_roads, self.wary_fords)):
            kinds = module.kinds_of(world)
            sites = [p["tile"] for p in world["places"] if p["kind"] == "danger"]
            flags = module.danger_tiles(sites, world.get("danger_reach", 0))
            cities = {p["key"]: p["tile"] for p in world["places"] if p["kind"] == "city"}
            for danger_cost in (0, 300):
                cost = module.derive(kinds, roads, world["rivers"], fords, flags, danger_cost)[0]
                reach = module.reachable(cost, module.index(*cities["capital"]))
                self.assertEqual([k for k, tile in cities.items() if not reach[module.index(*tile)]], [])

    def test_the_wary_roads_fork_round_a_danger_on_a_link_and_only_then(self):
        module = self.module
        kinds = [module.KIND["meadow"]] * (module.COLUMNS * module.ROWS)
        river = [False] * len(kinds)
        places = {"capital": (10, 40), "port": (60, 40)}
        with mock.patch.object(module, "ROADS", [("capital", "port")]):
            plain = module.build_roads(kinds, river, places)
            tiles = [t for line in plain for t in module.line_tiles(line)]
            centre = tiles[len(tiles) // 2]
            danger = module.danger_tiles([centre], 5)
            self.assertEqual(len(module.build_roads(kinds, river, places, [False] * len(kinds), 300)), 1)
            forked = module.build_roads(kinds, river, places, danger, 300)
        self.assertEqual(len(plain), 1)
        self.assertEqual(len(forked), 2)
        first, second = ({t for t in module.line_tiles(line)} for line in forked)
        self.assertTrue({(10, 40), (60, 40)} <= first)
        for end in (forked[1][0], forked[1][-1]):
            self.assertIn(end, first)
        self.assertFalse(any(danger[module.index(*t)] for t in second - first))

    def test_a_fork_keeps_to_its_road_until_the_danger_makes_it_leave(self):
        """A way round planned without seeing its own link's road can part from it at the city and run beside it; seen,
        the road is cheap, so the wary keep to it until near the danger. A fork's first tile off the road lies within
        the danger's reach and three tiles (a slanting road is left a little before the reach), and hugging the reach it
        never cuts into it, not even at a diagonal step's corner, which the road laid tile by tile would cross."""
        module = self.module
        kinds = [module.KIND["meadow"]] * (module.COLUMNS * module.ROWS)
        river = [False] * len(kinds)
        reach = 5
        for port in ((60, 40), (70, 52), (60, 58)):
            places = {"capital": (10, 40), "port": port}
            with mock.patch.object(module, "ROADS", [("capital", "port")]):
                tiles = [t for line in module.build_roads(kinds, river, places) for t in module.line_tiles(line)]
                centre = tiles[len(tiles) // 2]
                danger = module.danger_tiles([centre], reach)
                roads = module.build_roads(kinds, river, places, danger, 300)
            main = set(module.line_tiles(roads[0]))
            with self.subTest(port=port):
                self.assertGreater(len(roads), 1)
                for line in roads[1:]:
                    off = [t for t in module.line_tiles(line) if t not in main]
                    first = min(off, key=lambda t: math.dist(t, line[0]))
                    self.assertLessEqual(math.dist(first, centre), reach + 3)
                    self.assertEqual([t for t in off if danger[module.index(*t)]], [])

    def test_every_fork_ends_on_tiles_of_the_road_it_leaves(self):
        """The strong road is straightened after routing, so the tile where the wary way left its path may lie a tile off
        the written road; a fork has to meet the road as the game rasterises it."""
        module = self.module
        kinds = [module.KIND["meadow"]] * (module.COLUMNS * module.ROWS)
        river = [False] * len(kinds)
        places = {"capital": (10, 40), "port": (70, 52)}
        with mock.patch.object(module, "ROADS", [("capital", "port")]):
            plain = module.build_roads(kinds, river, places)
            tiles = [t for line in plain for t in module.line_tiles(line)]
            forks = 0
            for at in range(len(tiles) // 5, len(tiles), len(tiles) // 5):
                danger = module.danger_tiles([tiles[at]], 4)
                roads = module.build_roads(kinds, river, places, danger, 300)
                parent = set(module.line_tiles(roads[0]))
                for line in roads[1:]:
                    forks += 1
                    self.assertIn(line[0], parent)
                    self.assertIn(line[-1], parent)
        self.assertGreaterEqual(forks, 3)

    def test_every_fork_ends_on_waypoints_of_the_roads_before_it(self):
        """The game draws a road on a curve through its waypoints, which bulges off the tiles between far-apart ones; a
        fork's end on such a tile would miss the road as drawn, so each end is a waypoint of a road laid before it."""
        module = self.module
        kinds = [module.KIND["meadow"]] * (module.COLUMNS * module.ROWS)
        river = [False] * len(kinds)
        places = {"capital": (10, 40), "port": (70, 52)}
        with mock.patch.object(module, "ROADS", [("capital", "port")]):
            tiles = [t for line in module.build_roads(kinds, river, places) for t in module.line_tiles(line)]
            small = [module.link_roads(kinds, river, places, module.danger_tiles([tiles[at]], 4), 300)
                     for at in range(len(tiles) // 5, len(tiles), len(tiles) // 5)]
        unjoined = []
        for name, links in [*((f"small {n}", links) for n, links in enumerate(small)), ("shipped", self.links)]:
            before = []
            for strong, forked in links:
                before.extend(strong)
                for line in forked:
                    unjoined.extend(f"{name}: {line} at {end}" for end in (line[0], line[-1])
                                    if not any(end in other for other in before))
                    before.append(line)
        self.assertGreaterEqual(sum(len(forked) for links in small for _, forked in links), 3)
        self.assertEqual(unjoined, [])

    def test_a_comment_or_blank_line_at_column_zero_inside_a_block_is_refused(self):
        for key in ("roads", "fords"):
            for note in ("# a hand note", ""):
                lines = self.text.split("\n")
                at = lines.index(f"{key}:") + 2
                lines.insert(at, note)
                with self.subTest(key=key, note=note), self.assertRaisesRegex(SystemExit, f"line {at + 1} "):
                    self.module.rewrite_roads("\n".join(lines), self.roads, self.fords)

    def test_a_comment_after_the_last_road_stays(self):
        lines = self.text.split("\n")
        lines.insert(lines.index("rivers:"), "# about the rivers")
        out = self.module.rewrite_roads("\n".join(lines), self.roads, self.fords)
        self.assertIn("# about the rivers\nrivers:", out)

    def test_a_wrapped_travel_line_with_bands_gives_the_wary_cost(self):
        text = re.sub(
            r"(?m)^travel: .*\n(?:  .*\n)*",
            "travel: { share_per_mille: 60, stay_days: [2, 5], reach_base: 300, reach_per_power: 20, cultivator_power: 5,\n"
            "    bands: [{ from_power: 0.0, profile: wary }, { from_power: 5, profile: on_foot }] }\n", self.text)
        text = text.replace("profiles:\n", "profiles:\n  - { key: wary, ticks_per_cost: 16, features: { danger: 300 } }\n")
        self.assertNotEqual(text, self.text)
        self.assertEqual(self.module.wary_danger_cost(self.module.read_world(text)), 300)

    def test_scalars_read_as_numbers_and_booleans_without_their_comments(self):
        parse = self.module.parse_flow
        self.assertEqual(parse("6  # tiles")[0], 6)
        self.assertEqual(parse("0.5")[0], 0.5)
        self.assertEqual([parse(word)[0] for word in ("nan", "inf", "infinity", "1_0")], ["nan", "inf", "infinity", "1_0"])
        self.assertEqual(parse("{ a: false, b: true }")[0], {"a": False, "b": True})

    def test_a_reach_of_zero_flags_no_tile(self):
        self.assertFalse(any(self.module.danger_tiles([(40, 40)], 0)))
        self.assertTrue(self.module.danger_tiles([(40, 40)], 1)[self.module.index(40, 41)])

    def test_a_fork_ending_off_its_links_roads_is_refused_by_its_tile(self):
        module = self.module
        kinds = [module.KIND["meadow"]] * (module.COLUMNS * module.ROWS)
        river = [False] * len(kinds)
        places = {"capital": (10, 40), "port": (60, 40)}
        loose = module.index(31, 46)
        with mock.patch.object(module, "ROADS", [("capital", "port")]), \
                mock.patch.object(module, "forks", lambda detour, on_road: [[detour[0], module.index(30, 45), loose]]), \
                self.assertRaisesRegex(SystemExit, r"fork ends at \(31, 46\), off its link's roads"):
            module.link_roads(kinds, river, places, [False] * len(kinds), 300)


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
        # Never pushed and nothing committed yet: a session may have just made it; its build/ and session notes aren't tracked.
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
    tags lint cold, and a tag stores nothing."""

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


DAY = 86400.0
NOW = 1_800_000_000.0


class BuildSweepTest(unittest.TestCase):
    """Build trees unused for STALE_DAYS go; the native preset's everyday trees and the one about to build stay."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.build = Path(self.tmp.name) / "build"
        native = mock.patch.object(project_n, "NATIVE_PRESET", "linux-clang")
        native.start()
        self.addCleanup(native.stop)
        self.addCleanup(self.tmp.cleanup)

    def tree(self, rel, stamp=None, file_age=None):
        tree = self.build / rel
        tree.mkdir(parents=True)
        if stamp is not None:
            project_n.mark_used(tree, stamp)
        if file_age is not None:
            f = tree / ".objs" / "a.o"
            f.parent.mkdir(parents=True)
            f.write_bytes(b"x")
            os.utime(f, (NOW - file_age, NOW - file_age))
        return tree

    def sweep(self, **kwargs):
        with contextlib.redirect_stdout(io.StringIO()):
            return [tree for tree, _ in project_n.sweep_build(self.build, NOW, **kwargs)]

    def test_sweeps_a_tree_older_than_seven_days(self):
        old = self.tree("linux-gcc/debug", stamp=NOW - 8 * DAY)
        self.assertEqual(self.sweep(), [old])
        self.assertFalse(old.exists())
        self.assertFalse(old.parent.exists())  # an emptied preset folder goes too

    def test_keeps_a_tree_used_today(self):
        fresh = self.tree("linux-gcc/debug", stamp=NOW - 3600)
        self.assertEqual(self.sweep(), [])
        self.assertTrue(fresh.exists())

    def test_keeps_warm_trees_however_old(self):
        debug = self.tree("linux-clang/debug", stamp=NOW - 30 * DAY)
        release = self.tree("linux-clang/release", stamp=NOW - 30 * DAY)
        releasedbg = self.tree("linux-clang/releasedbg", stamp=NOW - 30 * DAY)
        self.assertEqual(self.sweep(), [releasedbg])
        self.assertTrue(debug.exists() and release.exists())

    def test_keeps_the_tree_about_to_be_used(self):
        old = self.tree("linux-gcc/release", stamp=NOW - 30 * DAY)
        self.assertEqual(self.sweep(keep=[old]), [])

    def test_sweeps_container_trees(self):
        old = self.tree("container/windows-msvc/release", stamp=NOW - 8 * DAY)
        self.assertEqual(self.sweep(), [old])

    def test_a_shorter_age_on_request(self):
        old = self.tree("linux-gcc/debug", stamp=NOW - 2 * DAY)
        self.assertEqual(self.sweep(days=1), [old])

    def test_unstamped_tree_uses_its_newest_file(self):
        recent = self.tree("linux-gcc/debug", file_age=2 * DAY)
        old = self.tree("linux-gcc/release", file_age=9 * DAY)
        self.assertEqual(self.sweep(), [old])
        self.assertTrue(recent.exists())

    def test_unreadable_stamp_falls_back(self):
        for text in ("garbage", ""):
            with self.subTest(text=text):
                tree = self.tree(f"linux-gcc/{text or 'empty'}", file_age=2 * DAY)
                stamp = tree / project_n.USED_STAMP
                stamp.write_text(text)
                os.utime(stamp, (NOW - 9 * DAY, NOW - 9 * DAY))  # not the real clock, which passes NOW one day
                self.assertEqual(project_n.last_used(tree), NOW - 2 * DAY)

    def test_ignores_what_no_command_owns(self):
        for rel in ("profiles/x", "measure/20261115", "coverage/x", "artifact/linux-gcc", "container/artifact/x"):
            self.tree(rel, stamp=NOW - 30 * DAY)
        self.assertEqual(self.sweep(), [])

    def test_missing_or_empty_build_folder(self):
        self.assertEqual(self.sweep(), [])
        self.build.mkdir()
        (self.build / "linux-gcc").mkdir()
        (self.build / "linux-gcc" / "stray.txt").write_text("not a tree")
        self.assertEqual(self.sweep(), [])

    def test_sweep_here_skips_ci_and_containers(self):
        for env, container in (({"GITHUB_ACTIONS": "true"}, False), ({}, True)):
            with self.subTest(env=env, container=container), mock.patch.dict(os.environ, env), \
                    mock.patch.object(project_n, "IN_CONTAINER", container), mock.patch.object(project_n, "swept", False), \
                    mock.patch.object(project_n, "sweep_build") as sweep:
                if not env:
                    os.environ.pop("GITHUB_ACTIONS", None)
                project_n.sweep_here()
                sweep.assert_not_called()

    def test_a_tree_that_will_not_go_is_left_and_the_sweep_goes_on(self):
        locked = self.tree("linux-gcc/debug", stamp=NOW - 8 * DAY)
        old = self.tree("linux-gcc/release", stamp=NOW - 8 * DAY)
        rmtree = project_n.shutil.rmtree

        def refuse_locked(path, *args, **kwargs):
            if Path(path) == locked:
                raise PermissionError(13, "in use", str(path))
            rmtree(path, *args, **kwargs)

        with mock.patch.object(project_n.shutil, "rmtree", refuse_locked):
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                removed = [tree for tree, _ in project_n.sweep_build(self.build, NOW)]
        self.assertEqual(removed, [old])
        self.assertTrue(locked.exists())
        self.assertIn(str(Path("build", "linux-gcc", "debug")), out.getvalue())

    def run_swept(self, command, args, **patches):
        """`command` with ROOT at self.build's parent, sweeping for real but building nothing."""
        with mock.patch.multiple(project_n, ROOT=self.build.parent, swept=False, **patches), \
                mock.patch.dict(os.environ), contextlib.redirect_stdout(io.StringIO()):
            os.environ.pop("GITHUB_ACTIONS", None)
            command(args)

    def test_sweeps_once_per_run(self):
        with mock.patch.multiple(project_n, swept=False, IN_CONTAINER=False, ROOT=self.build.parent), \
                mock.patch.dict(os.environ), mock.patch.object(project_n, "sweep_build") as sweep:
            os.environ.pop("GITHUB_ACTIONS", None)
            project_n.sweep_here([Path("build/linux-gcc/release")])
            project_n.sweep_here([Path("build/linux-gcc/debug")])  # a command the first one runs
        sweep.assert_called_once()
        self.assertEqual(sweep.call_args.kwargs["keep"], [self.build / "linux-gcc" / "release"])

    def test_a_run_stamps_its_tree_before_it_builds(self):
        # A sweep in another terminal must not take a tree this run is still starting up to build (a container's
        # re-entry, or check's later steps).
        used = self.tree(Path("container") / "windows-msvc" / "release", stamp=time.time() - 30 * DAY)
        twin = self.tree(Path("windows-msvc") / "release", stamp=time.time() - 30 * DAY)
        self.run_swept(project_n.cmd_test, argparse.Namespace(
            preset="windows-msvc", mode="release", sanitizer="none", container=False, target=None, test=None, args=[],
            list_tests=False), needs_container=lambda *a: True, reenter=lambda *a: None)
        self.assertGreater(project_n.last_used(used), time.time() - 60)
        self.assertFalse(twin.exists())  # the run builds in the container, so the stale native tree goes

    def test_build_all_keeps_every_presets_tree(self):
        trees = [self.tree(Path("linux-gcc") / "release", stamp=time.time() - 30 * DAY),
                 self.tree(Path("container") / "windows-msvc" / "release", stamp=time.time() - 30 * DAY)]
        unused = self.tree(Path("linux-gcc") / "releasedbg", stamp=time.time() - 30 * DAY)
        self.run_swept(project_n.cmd_build, argparse.Namespace(preset="all", mode="release", sanitizer="none", container=False),
                       build=lambda *a, **k: None, buildable_here=lambda preset: True, reenter=lambda *a: None,
                       needs_container=lambda preset, force=False: preset == "windows-msvc",
                       check_sanitizer=lambda *a: None)
        self.assertEqual([t.exists() for t in trees], [True, True])
        self.assertFalse(unused.exists())

    def test_check_keeps_every_tree_its_steps_build(self):
        for container in (False, True):
            with self.subTest(container=container):
                if self.build.exists():
                    project_n.shutil.rmtree(self.build)
                self.check_keeps_its_trees(container)

    def check_keeps_its_trees(self, container):
        used = []

        def record(args):
            used.extend(project_n.own_trees([args.preset], args.mode, args.sanitizer, args.container))

        def record_reference(args):
            used.extend(project_n.own_trees([args.preset], "release", container=args.container))

        leaves = ("debug", "release", "releasedbg", "debug-address")
        created = [self.tree(Path(*parent, preset, leaf), stamp=time.time() - 30 * DAY)
                   for parent in ((), ("container",)) for preset in project_n.PRESETS for leaf in leaves]
        self.run_swept(project_n.cmd_check, argparse.Namespace(container=container, sanitizers=False),
                       clone_set_up=lambda: True, cmd_selftest=lambda args: None, commit_range=lambda: None,
                       cmd_fmt=lambda args: None, cmd_lint=lambda args: None, cmd_reference=record_reference,
                       cmd_test=record, cmd_build=record, buildable_here=lambda preset: True,
                       needs_container=lambda preset, force=False: force or preset == "windows-msvc")
        lint = Path("build", *(("container",) if container else ()), project_n.LINT_PRESET, "debug")
        kept = {self.build.parent / t for t in used + [lint]} | project_n.warm_trees(self.build)
        # A tree only check's steps protect: under --container, the container's, so the native twin goes.
        gcc = self.build / ("container" if container else "") / "linux-gcc" / "release"
        self.assertIn(gcc, kept)
        self.assertEqual({t for t in created if t.exists()}, {t for t in created if t in kept})
        self.assertTrue(any(not t.exists() for t in created))

    def test_clean_refuses_options_stale_would_drop(self):
        base = {"stale": False, "days": None, "preset": None, "packages": False, "images": False}
        for wrong in ({"stale": True, "preset": "linux-gcc"}, {"stale": True, "packages": True},
                      {"stale": True, "images": True}, {"days": 3}):
            with self.subTest(wrong=wrong), mock.patch.object(project_n, "ROOT", self.build.parent), \
                    self.assertRaises(project_n.Fail):
                project_n.cmd_clean(argparse.Namespace(**{**base, **wrong}))

    def test_configure_stamps_its_tree(self):
        with temp_repo({"xmake-requires.lock": "x"}) as root, mock.patch.object(project_n, "run"), \
                mock.patch.object(project_n, "xmake_env", lambda bdir: {}), \
                mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop(project_n.BUILD_CACHE_ENV, None)
            project_n.configure("linux-gcc", "release")
            self.assertGreater(project_n.last_used(root / "build" / "linux-gcc" / "release"), time.time() - 60)
            self.assertTrue((root / "build" / "linux-gcc" / "release" / project_n.USED_STAMP).is_file())


class BuildCacheAgeTest(unittest.TestCase):
    """A build cache nothing reads goes whole; in the others, entries written over CACHE_DAYS ago go, a key with its
    warnings."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.build = Path(self.tmp.name) / "build"

    def entry(self, tree, name, age):
        f = self.build / tree / ".build_cache" / name[:2] / name
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_bytes(b"object")
        os.utime(f, (NOW - age, NOW - age))
        project_n.mark_used(self.build / tree, NOW)
        return f

    def sweep(self):
        with contextlib.redirect_stdout(io.StringIO()):
            project_n.sweep_build(self.build, NOW)

    def test_removes_dead_cache_of_gcc_and_msvc(self):
        for tree in ("linux-gcc/debug", "container/windows-msvc/release"):
            self.entry(tree, "ab01", 0)
        self.sweep()
        self.assertFalse((self.build / "linux-gcc/debug/.build_cache").exists())
        self.assertFalse((self.build / "container/windows-msvc/release/.build_cache").exists())
        self.assertTrue((self.build / "linux-gcc/debug").is_dir())

    def test_ages_clang_entries(self):
        old = [self.entry("linux-clang/debug", n, 8 * DAY) for n in ("ab01", "ab01.txt")]
        new = [self.entry("linux-clang/debug", n, 1 * DAY) for n in ("ab02", "ab02.txt")]
        self.sweep()
        self.assertEqual([f.exists() for f in old + new], [False, False, True, True])

    def test_never_splits_an_entry(self):
        old_key = [self.entry("container/linux-clang/release", "cd03", 15 * DAY),
                   self.entry("container/linux-clang/release", "cd03.txt", 1 * DAY)]
        new_key = [self.entry("container/linux-clang/release", "cd04", 1 * DAY),
                   self.entry("container/linux-clang/release", "cd04.txt", 15 * DAY)]
        self.sweep()
        self.assertEqual([f.exists() for f in old_key + new_key], [False, False, True, True])

    def test_removes_old_orphan_warnings_and_empty_folders(self):
        orphan = self.entry("linux-clang/debug", "ef05.txt", 15 * DAY)
        self.sweep()
        self.assertFalse(orphan.parent.exists())
        self.assertTrue((self.build / "linux-clang/debug/.build_cache").is_dir())

    def test_leaves_ci_cache_alone(self):
        ci = Path(self.tmp.name) / "ci-cache" / "linux-clang-debug" / "ab" / "ab06"
        ci.parent.mkdir(parents=True)
        ci.write_bytes(b"object")
        os.utime(ci, (NOW - 30 * DAY, NOW - 30 * DAY))
        self.entry("linux-clang/debug", "ab07", 0)
        self.sweep()
        self.assertTrue(ci.exists())


if __name__ == "__main__":
    unittest.main()
