"""Self-checks for project_n.py. Run: pixi run python -m unittest discover -s tools"""

import unittest

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
            result = project_n.build_all(project_n.argparse.Namespace(preset="all", mode="debug", sanitizer="none", container=False))
        finally:
            project_n.cmd_build = saved
        self.assertEqual(built, [p for p in project_n.PRESETS if project_n.buildable_here(p)])
        self.assertEqual(result, 1)


class DocsTest(unittest.TestCase):
    def test_agent_docs_quote_only_real_commands(self):
        self.assertEqual(project_n.stale_doc_commands(), [])


class SuppressionTest(unittest.TestCase):
    def test_sources_have_no_unexplained_suppressions(self):
        self.assertEqual(project_n.unexplained_suppressions(), [])

    def test_nolint_needs_checks_and_a_reason(self):
        explained = project_n.nolint_explained
        self.assertTrue(explained("f(); // NOLINT(cppcoreguidelines-pro-type-vararg): C API takes varargs"))
        self.assertTrue(explained("// NOLINTEND(bugprone-macro-parentheses)"))
        self.assertFalse(explained("f(); // NOLINT"))
        self.assertFalse(explained("// NOLINTNEXTLINE(readability-magic-numbers)"))

    def test_recognises_diagnostic_pragmas(self):
        for line in ("#pragma clang diagnostic ignored \"-Wshadow\"", "#  pragma GCC diagnostic ignored \"-Wconversion\"",
                     "#pragma warning(disable: 4996)"):
            self.assertRegex(line, project_n.PRAGMA_RE)
        self.assertNotRegex("#pragma clang diagnostic push", project_n.PRAGMA_RE)


class SanitizerTest(unittest.TestCase):
    def test_each_sanitizer_gets_its_own_build_dir(self):
        self.assertEqual(project_n.build_dir("linux-gcc", "debug").name, "debug")
        self.assertEqual(project_n.build_dir("linux-gcc", "debug", "address").name, "debug-address")

    def test_support_matrix_is_enforced(self):
        project_n.check_sanitizer("linux-clang", "thread")
        project_n.check_sanitizer("windows-msvc", "none")
        with self.assertRaises(project_n.Fail):
            project_n.check_sanitizer("windows-msvc", "undefined")

    def test_ubsan_findings_fail_the_run(self):
        self.assertIn("halt_on_error=1", project_n.xmake_env(project_n.build_dir("linux-gcc", "debug"))["UBSAN_OPTIONS"])


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

    def test_msvc_install_follows_only_its_containerfile(self):
        self.assertRegex(project_n.msvc_dir().name, r"^msvc-[0-9a-f]{12}$")

    def test_container_command_mounts_repo_and_cache(self):
        cmd = project_n.container_cmd("img", ["pixi", "run", "project_n", "build"])
        self.assertIn(f"{project_n.ROOT}:/src:Z", cmd)
        self.assertIn(f"{project_n.CACHE_VOLUME}:/home/dev/.xmake", cmd)
        self.assertEqual(cmd[-5:], ["img", "pixi", "run", "project_n", "build"])


if __name__ == "__main__":
    unittest.main()
