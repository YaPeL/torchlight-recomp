#!/usr/bin/env python3
"""Tests for run_capped.py with small Python children, no game: each way a run can end, and
nothing left behind (a grandchild included)."""

import os
import pathlib
import subprocess
import sys
import tempfile
import time
import unittest

HERE = pathlib.Path(__file__).resolve().parent
SCRIPT = HERE / "run_capped.py"

# A child that starts a grandchild sleeping and writes the grandchild's pid to `pid_file`.
SPAWN_GRANDCHILD = """
import subprocess, sys, time
grandchild = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(600)'])
open(sys.argv[1], 'w').write(str(grandchild.pid))
{rest}
"""


def alive(pid):
    if os.name == "nt":
        out = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH"], capture_output=True,
                             text=True).stdout
        return str(pid) in out
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    # A zombie no one reaps (the grandchild's parent is gone, init reaps it soon) is not running.
    state = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)], capture_output=True,
                           text=True).stdout.strip()
    return bool(state) and not state.startswith("Z")


class RunCappedTest(unittest.TestCase):
    def setUp(self):
        self.dir = pathlib.Path(tempfile.mkdtemp(prefix="run_capped_test_"))

    def run_capped(self, *options, code):
        command = [sys.executable, str(SCRIPT), "--output", str(self.dir / "out.txt"), *options,
                   "--", sys.executable, "-c", code]
        return subprocess.run(command, capture_output=True, text=True, timeout=120)

    def assert_gone(self, pid_file):
        pid = int(pid_file.read_text())
        deadline = time.monotonic() + 5
        while alive(pid) and time.monotonic() < deadline:
            time.sleep(0.1)
        self.assertFalse(alive(pid), "the grandchild outlived the run")

    def test_exit_code_passes_through(self):
        result = self.run_capped(code="import sys; print('hello'); sys.exit(7)")
        self.assertEqual(result.returncode, 7, result.stderr)
        self.assertIn("hello", (self.dir / "out.txt").read_text())
        self.assertIn("nothing of the command is left", result.stderr)

    def test_time_limit_kills_the_whole_tree(self):
        pid_file = self.dir / "grandchild.pid"
        code = SPAWN_GRANDCHILD.format(rest="time.sleep(600)")
        result = self.run_capped("--timeout", "2", code=code.replace("sys.argv[1]",
                                                                      repr(str(pid_file))))
        self.assertEqual(result.returncode, 124, result.stderr)
        self.assertIn("time limit", result.stderr)
        self.assert_gone(pid_file)

    def test_log_limit_stops_a_runaway_log(self):
        log = self.dir / "logs" / "game.log"
        log.parent.mkdir()
        code = (f"f = open({str(log)!r}, 'w')\n"
                "while True:\n    f.write('x' * 65536); f.flush()\n")
        start = time.monotonic()
        result = self.run_capped("--timeout", "60", "--max-log-mb", "2", "--watch",
                                 str(log.parent), code=code)
        self.assertEqual(result.returncode, 125, result.stderr)
        self.assertLess(time.monotonic() - start, 30)
        # Stopped, not grown without bound: past the limit by at most what a disk-speed writer
        # adds in one check interval (0.1 s) and the stop.
        self.assertLess(log.stat().st_size, 512 * 2**20)

    def test_stdout_is_watched_too(self):
        code = "import sys\nwhile True:\n    sys.stdout.write('y' * 65536); sys.stdout.flush()\n"
        result = self.run_capped("--timeout", "60", "--max-log-mb", "1", code=code)
        self.assertEqual(result.returncode, 125, result.stderr)

    def test_a_child_left_behind_after_exit_is_killed(self):
        pid_file = self.dir / "grandchild.pid"
        code = SPAWN_GRANDCHILD.format(rest="sys.exit(0)")
        result = self.run_capped("--timeout", "30", code=code.replace("sys.argv[1]",
                                                                       repr(str(pid_file))))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_gone(pid_file)

    def test_an_old_log_does_not_count(self):
        old = self.dir / "old.log"
        old.write_bytes(b"z" * (3 * 2**20))
        os.utime(old, (time.time() - 3600, time.time() - 3600))
        result = self.run_capped("--max-log-mb", "1", "--watch", str(self.dir), code="pass")
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
