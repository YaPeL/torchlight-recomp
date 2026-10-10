#!/usr/bin/env python3
"""Tests for run_capped.py with small Python children, no game: each way a run can end, and
nothing left behind (a grandchild included)."""

import os
import pathlib
import shutil
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
    # A zombie no one reaps (the grandchild's parent is gone; a container's PID 1 may never reap
    # it) is not running. /proc where there is one: a minimal container has no ps.
    stat = pathlib.Path(f"/proc/{pid}/stat")
    if stat.parent.parent.joinpath("self", "stat").exists():
        try:
            text = stat.read_text()
        except OSError:
            return False
        return text[text.rindex(")") + 2:].split()[0] != "Z"
    state = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)], capture_output=True,
                           text=True).stdout.strip()
    return bool(state) and not state.startswith("Z")


class RunCappedTest(unittest.TestCase):
    def setUp(self):
        self.dir = pathlib.Path(tempfile.mkdtemp(prefix="run_capped_test_"))
        self.addCleanup(shutil.rmtree, self.dir, ignore_errors=True)

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

    # A fake game log, written like the SDK's: game.log rotated to game.1.log ... game.<parts>.log,
    # the oldest part deleted. `body` writes with log(text) and rotate().
    def fake_game(self, body, parts=2):
        folder = self.dir / "logs"
        folder.mkdir(exist_ok=True)
        return (f"import os, time\n"
                f"folder, parts = {str(folder)!r}, {parts}\n"
                "f = open(os.path.join(folder, 'game.log'), 'w')\n"
                "def log(text):\n    f.write(text + '\\n'); f.flush()\n"
                "def rotate():\n"
                "    global f\n"
                "    f.close()\n"
                "    for k in range(parts, 0, -1):\n"
                "        old = os.path.join(folder, f'game.{k - 1}.log' if k > 1 else 'game.log')\n"
                "        if os.path.exists(old):\n"
                "            os.replace(old, os.path.join(folder, f'game.{k}.log'))\n"
                "    f = open(os.path.join(folder, 'game.log'), 'w')\n"
                f"{body}\n"), folder

    def test_log_limit_counts_parts_rotated_away(self):
        # Each part stays under 1 MB and at most three exist: only the bytes written count.
        code, folder = self.fake_game("while True:\n    log('x' * 1000)\n"
                                      "    if f.tell() > 512 * 1024: rotate()")
        result = self.run_capped("--timeout", "60", "--max-log-mb", "4", "--watch", str(folder),
                                 code=code)
        self.assertEqual(result.returncode, 125, result.stderr)
        self.assertLess(sum(p.stat().st_size for p in folder.iterdir()), 3 * 2**20)

    def test_a_repeated_fault_stops_the_run(self):
        fault = "[error] [sys] Unhandled guest access violation: read of guest 0x000000B4"
        # The start is written before the flood, as the game's is; a part rotated away between two
        # checks (0.1 s) is not read, so the flood itself rotates as fast as it likes.
        code, folder = self.fake_game("log('mods: 29 mods'); time.sleep(0.5)\n"
                                      f"while True:\n    log('[t1] ' + {fault!r}); rotate()\n"
                                      "    time.sleep(0.001)", parts=3)
        start = time.monotonic()
        result = self.run_capped("--timeout", "60", "--max-repeats", "50", "--watch", str(folder),
                                 code=code)
        self.assertEqual(result.returncode, 123, result.stderr)
        self.assertIn("0x000000B4", result.stderr)
        self.assertLess(time.monotonic() - start, 30)
        # The first line is long gone from the rotated logs, but the head copy has it.
        head = (self.dir / "out.txt.head").read_text()
        self.assertIn("mods: 29 mods", head)
        self.assertFalse(any("mods: 29" in p.read_text() for p in folder.iterdir()))

    def test_different_faults_are_counted_apart(self):
        code, folder = self.fake_game(
            "for i in range(300):\n"
            "    log(f'Unhandled guest access violation: read of guest {i:#x}')\nlog('done')")
        result = self.run_capped("--timeout", "60", "--max-repeats", "5", "--watch", str(folder),
                                 code=code)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_stop_pattern_seen_across_a_rotation(self):
        pid_file = self.dir / "grandchild.pid"
        code, folder = self.fake_game(
            "import subprocess, sys\n"
            "g = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(600)'])\n"
            f"open({str(pid_file)!r}, 'w').write(str(g.pid))\n"
            "log('loaded the unit index'); rotate()\n"
            "for i in range(100):\n    log('after ' * 50)\n"
            "time.sleep(600)")
        start = time.monotonic()
        result = self.run_capped("--timeout", "60", "--stop-on", "loaded the unit index",
                                 "--stop-delay", "1", "--watch", str(folder), code=code)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("stop pattern seen", result.stderr)
        self.assertLess(time.monotonic() - start, 30)
        self.assert_gone(pid_file)

    @unittest.skipUnless(sys.platform.startswith("linux"), "PR_SET_CHILD_SUBREAPER is Linux's")
    def test_zombies_left_unreaped_do_not_count_as_alive(self):
        # As in a container whose PID 1 never reaps orphans (CI's ubuntu:22.04 job): the runner is
        # made the subreaper of what it starts, so the killed grandchild is left a zombie of the
        # runner, which only waits for the command itself. A zombie-only group is gone; the runner
        # used to report it NOT KILLED (126).
        pid_file = self.dir / "grandchild.pid"
        code = SPAWN_GRANDCHILD.format(rest="time.sleep(600)").replace("sys.argv[1]",
                                                                       repr(str(pid_file)))
        wrapper = ("import ctypes, os, sys\n"
                   "if ctypes.CDLL(None, use_errno=True).prctl(36, 1, 0, 0, 0) != 0:\n"
                   "    sys.exit('prctl(PR_SET_CHILD_SUBREAPER) failed')\n"
                   "os.execv(sys.executable, [sys.executable] + sys.argv[1:])\n")
        result = subprocess.run([sys.executable, "-c", wrapper, str(SCRIPT), "--output",
                                 str(self.dir / "out.txt"), "--timeout", "2", "--",
                                 sys.executable, "-c", code],
                                capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 124, result.stderr)
        self.assertIn("nothing of the command is left", result.stderr)
        self.assert_gone(pid_file)

    @unittest.skipIf(os.name == "nt", "process groups are POSIX")
    def test_a_child_in_another_group_is_killed(self):
        # gdb runs the program in a process group of its own: killing the group misses it.
        pid_file = self.dir / "grandchild.pid"
        code = ("import subprocess, sys, time\n"
                "g = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(600)'],\n"
                "                     start_new_session=True)\n"
                f"open({str(pid_file)!r}, 'w').write(str(g.pid))\n"
                "time.sleep(600)")
        result = self.run_capped("--timeout", "3", code=code)
        self.assertEqual(result.returncode, 124, result.stderr)
        self.assert_gone(pid_file)

    @unittest.skipIf(os.name == "nt", "POSIX signals")
    def test_an_interrupted_script_stops_the_command(self):
        # Interrupting the runner (Ctrl+C, a kill from a script driving it) once ended it at once and
        # left the game running in its own session.
        import signal
        for sig in (signal.SIGINT, signal.SIGTERM):
            pid_file = self.dir / f"grandchild{int(sig)}.pid"
            code = SPAWN_GRANDCHILD.format(rest="time.sleep(600)").replace("sys.argv[1]",
                                                                           repr(str(pid_file)))
            runner = subprocess.Popen([sys.executable, str(SCRIPT), "--output",
                                       str(self.dir / "out.txt"), "--timeout", "60", "--",
                                       sys.executable, "-c", code],
                                      stderr=subprocess.PIPE, text=True)
            deadline = time.monotonic() + 20
            while not pid_file.exists() and time.monotonic() < deadline:
                time.sleep(0.1)
            time.sleep(0.3)  # the pid is written before the file is closed
            runner.send_signal(sig)
            _, err = runner.communicate(timeout=60)
            self.assertEqual(runner.returncode, 130, err)
            self.assertIn("interrupted", err)
            self.assert_gone(pid_file)

    def test_a_file_there_before_counts_only_what_it_grows(self):
        # A log appended across runs: its old content is not this run's.
        log = self.dir / "logs" / "shared.log"
        log.parent.mkdir()
        log.write_bytes(b"old line\n" * (3 * 2**20 // 9))
        code = (f"import time\nf = open({str(log)!r}, 'a')\n"
                "f.write('new line\\n' * 1000); f.flush()\ntime.sleep(600)\n")
        result = self.run_capped("--timeout", "60", "--max-log-mb", "1", "--watch",
                                 str(log.parent), "--stop-on", "new line", "--stop-delay", "1",
                                 code=code)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_a_file_being_read_can_be_renamed(self):
        # The writer's rotation renames the log while the runner reads it; on Windows a plain
        # open() keeps that from happening (no FILE_SHARE_DELETE).
        sys.path.insert(0, str(HERE))
        import run_capped
        log = self.dir / "game.log"
        log.write_bytes(b"line\n" * 100)
        with run_capped.open_shared(log) as handle:
            first = handle.read(5)
            os.replace(log, self.dir / "game.1.log")
            rest = handle.read()
        self.assertEqual(first, b"line\n")
        self.assertEqual(len(rest), 5 * 99)
        self.assertTrue((self.dir / "game.1.log").exists())


if __name__ == "__main__":
    unittest.main()
