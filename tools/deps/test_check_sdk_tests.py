#!/usr/bin/env python3
"""Tests for check_sdk_tests.py with small JUnit files as ctest writes them (ctest 4.2:
status "run", "fail" or "notrun", and for a skip return code a <skipped> with
message="SKIP_RETURN_CODE=N")."""

import pathlib
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
SCRIPT = HERE / "check_sdk_tests.py"

PASSED = '<testcase name="{}" status="run"/>'
FAILED = '<testcase name="{}" status="fail"><failure message="Failed"/></testcase>'
SKIPPED = '<testcase name="{}" status="notrun"><skipped message="SKIP_RETURN_CODE=4"/></testcase>'
NOT_RUN = ('<testcase name="{}" status="notrun">'
           '<skipped message="Unable to find executable"/></testcase>')

SKIPS = """# comment
windows | unit.Case variants | NTFS ignores case
macos | unit.Case variants | APFS ignores case
"""


class CheckSdkTests(unittest.TestCase):
    def run_check(self, platform, *cases):
        with tempfile.TemporaryDirectory() as folder:
            junit = pathlib.Path(folder) / "tests.xml"
            junit.write_text('<?xml version="1.0"?><testsuite>' + "".join(cases) + "</testsuite>")
            skips = pathlib.Path(folder) / "skips.txt"
            skips.write_text(SKIPS)
            result = subprocess.run([sys.executable, str(SCRIPT), str(junit), platform, str(skips)],
                                    capture_output=True, text=True)
            return result.returncode, result.stderr

    def test_all_pass(self):
        code, _ = self.run_check("linux", PASSED.format("unit.a"), PASSED.format("ppc.b"))
        self.assertEqual(code, 0)

    def test_listed_skip_on_its_platform(self):
        code, _ = self.run_check("windows", PASSED.format("unit.a"),
                                 SKIPPED.format("unit.Case variants"))
        self.assertEqual(code, 0)

    def test_skip_not_listed_for_this_platform(self):
        code, err = self.run_check("linux", PASSED.format("unit.a"),
                                   SKIPPED.format("unit.Case variants"))
        self.assertEqual(code, 1)
        self.assertIn("skipped, not listed for linux: unit.Case variants", err)

    def test_listed_skip_that_ran(self):
        code, err = self.run_check("macos", PASSED.format("unit.Case variants"))
        self.assertEqual(code, 1)
        self.assertIn("but it ran: unit.Case variants", err)

    def test_listed_skip_missing_from_the_run(self):
        code, err = self.run_check("macos", PASSED.format("unit.a"))
        self.assertEqual(code, 1)
        self.assertIn("but not in the run: unit.Case variants", err)

    def test_failure(self):
        code, err = self.run_check("linux", FAILED.format("unit.a"))
        self.assertEqual(code, 1)
        self.assertIn("failed: unit.a", err)

    def test_not_run_is_not_a_skip(self):
        # Also when the platform lists the test: only a skip return code is a skip.
        code, err = self.run_check("windows", NOT_RUN.format("unit.Case variants"))
        self.assertEqual(code, 1)
        self.assertIn("did not run (notrun: Unable to find executable)", err)

    def test_words_in_a_name_do_not_count(self):
        code, _ = self.run_check("linux", PASSED.format("unit.Skipped and Not Run did not run"))
        self.assertEqual(code, 0)

    def test_empty_run(self):
        code, err = self.run_check("linux")
        self.assertEqual(code, 1)
        self.assertIn("no tests in the run", err)

    def test_bad_skips_line(self):
        with tempfile.TemporaryDirectory() as folder:
            junit = pathlib.Path(folder) / "tests.xml"
            junit.write_text('<testsuite>' + PASSED.format("unit.a") + "</testsuite>")
            skips = pathlib.Path(folder) / "skips.txt"
            skips.write_text("windows | no reason\n")
            result = subprocess.run([sys.executable, str(SCRIPT), str(junit), "linux", str(skips)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)

    def test_the_project_list_parses(self):
        code, _ = self.run_check("linux", PASSED.format("unit.a"))
        self.assertEqual(code, 0)
        result = subprocess.run(
            [sys.executable, "-c",
             "import sys; sys.path.insert(0, sys.argv[1]); import check_sdk_tests as c; "
             "[c.read_allowed(sys.argv[2], p) for p in c.PLATFORMS]",
             str(HERE), str(HERE / "sdk_test_skips.txt")],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
