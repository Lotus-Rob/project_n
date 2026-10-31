"""Self-checks for project_n.py. Run: pixi run python -m unittest discover -s tools"""

import contextlib
import functools
import io
import subprocess
import tempfile
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

    def test_a_changed_header_lints_every_source_file(self):
        with temp_repo({"src/a.cpp": "a", "src/b.cpp": "b", "src/a.hpp": "h"}):
            self.assertEqual(project_n.lint_units(["src/a.hpp"]), ["src/a.cpp", "src/b.cpp"])

    def test_a_changed_source_file_lints_only_itself(self):
        with temp_repo({"src/a.cpp": "a", "src/b.cpp": "b"}):
            self.assertEqual(project_n.lint_units(["src/a.cpp"]), ["src/a.cpp"])


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
    def test_check_refuses_without_the_hooks(self):
        ran = []
        with mock.patch.object(project_n, "hooks_enabled", lambda: False), \
             mock.patch.object(project_n, "cmd_selftest", lambda args: ran.append("selftest")), \
             self.assertRaises(project_n.Fail):
            project_n.cmd_check(project_n.argparse.Namespace(container=False, sanitizers=False))
        self.assertEqual(ran, [])  # refused before running anything

    def test_container_check_runs_unity_once_on_a_preset_with_an_image(self):
        calls = []
        with mock.patch.multiple(project_n, hooks_enabled=lambda: True, cmd_selftest=lambda args: None,
                                 commit_range=lambda: None, cmd_fmt=lambda args: None, cmd_lint=lambda args: None,
                                 cmd_test=calls.append), contextlib.redirect_stdout(io.StringIO()):
            project_n.cmd_check(project_n.argparse.Namespace(container=True, sanitizers=False))
        unity = [args for args in calls if args.unity]
        self.assertEqual(len(unity), 1)
        self.assertTrue(project_n.PRESETS[unity[0].preset]["image"], unity[0].preset)


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

    def test_a_facade_including_another_library_is_reported(self):
        with temp_repo({"src/core/log.hpp": "#pragma once\n#include <tracy/Tracy.hpp>\n"}):
            self.assertEqual(project_n.leaked_dependencies(["src/core/log.hpp"]), ["src/core/log.hpp:2: #include <tracy/Tracy.hpp>"])


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
                                                            target=None, args=[], unity=True))
        self.assertIn("--unity", argvs[0])

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


if __name__ == "__main__":
    unittest.main()
