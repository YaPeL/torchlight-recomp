#!/usr/bin/env python3
"""Tests for profile_window.py on a log shaped as the game writes it (our own lines only)."""

import datetime
import pathlib
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import profile_window  # noqa: E402

LOG = """\
[2026-10-22 22:48:08.845] [info] [core] [t9760] perf step 0: rest
[2026-10-22 22:48:18.900] [info] [core] [t9761] frame time (guest swap to swap): 800 frames
[2026-10-22 22:48:29.006] [info] [core] [t9760] perf step 'rest': 1536 frames, 76.2 fps
[2026-10-22 22:48:29.006] [info] [core] [t9760] perf step 1: fight
[2026-10-22 22:48:59.100] [info] [core] [t9760] perf step 'fight': 1571 frames, 42.7 fps
""".splitlines(keepends=True)


def t(clock):
    return datetime.datetime.strptime("2026-10-22 " + clock, "%Y-%m-%d %H:%M:%S.%f").timestamp()


class ProfileWindowTest(unittest.TestCase):
    def test_inside_the_fight(self):
        ok, _ = profile_window.check(LOG, t("22:48:31.000"), t("22:48:56.000"))
        self.assertTrue(ok)

    def test_started_on_the_rest_mark(self):
        # What the broken `grep -q "perf step 1"` did: it matched "perf" in the rest mark.
        ok, msg = profile_window.check(LOG, t("22:48:10.900"), t("22:48:35.900"))
        self.assertFalse(ok)
        self.assertIn("not entirely inside", msg)

    def test_runs_past_the_end(self):
        ok, _ = profile_window.check(LOG, t("22:48:40.000"), t("22:49:05.000"))
        self.assertFalse(ok)

    def test_no_fight_in_the_log(self):
        ok, msg = profile_window.check(LOG[:3], t("22:48:31.000"), t("22:48:40.000"))
        self.assertFalse(ok)
        self.assertIn("no complete", msg)

    def test_run_stopped_before_the_summary(self):
        stopped = LOG[:4] + ["[2026-10-22 22:48:50.000] [info] [core] [t9761] frame time\n"]
        ok, _ = profile_window.check(stopped, t("22:48:31.000"), t("22:48:45.000"))
        self.assertFalse(ok)
        ok, _ = profile_window.check(stopped, t("22:48:31.000"), t("22:48:45.000"), open_end=True)
        self.assertTrue(ok)
        ok, _ = profile_window.check(stopped, t("22:48:31.000"), t("22:48:55.000"), open_end=True)
        self.assertFalse(ok)

    def test_ends_before_it_starts(self):
        ok, msg = profile_window.check(LOG, t("22:48:40.000"), t("22:48:35.000"))
        self.assertFalse(ok)
        self.assertIn("ends before it starts", msg)

    def test_command_line_exit_codes(self):
        script = pathlib.Path(profile_window.__file__)
        with tempfile.TemporaryDirectory() as d:
            log = pathlib.Path(d) / "run.log"
            log.write_text("".join(LOG), encoding="utf-8")
            run = lambda *a: subprocess.run([sys.executable, "-I", str(script), *a],
                                            capture_output=True, text=True)
            inside = run(str(log), str(t("22:48:31.000")), str(t("22:48:56.000")))
            self.assertEqual(inside.returncode, 0)
            self.assertTrue(inside.stdout.startswith("ok: "))
            on_rest = run(str(log), str(t("22:48:10.900")), str(t("22:48:35.900")))
            self.assertEqual(on_rest.returncode, 1)
            self.assertTrue(on_rest.stdout.startswith("error: "))
            missing = run(str(pathlib.Path(d) / "none.log"), "0", "1")
            self.assertEqual(missing.returncode, 1)
            self.assertTrue(missing.stdout.startswith("error: "))

    def test_the_rest_step_by_name(self):
        ok, _ = profile_window.check(LOG, t("22:48:10.000"), t("22:48:20.000"), step="rest")
        self.assertTrue(ok)


if __name__ == "__main__":
    unittest.main()
