#!/usr/bin/env python3
"""Runs the game (or any command) with hard limits, for every agent and platform.

A run ends at the first of: the command exits, the time limit passes, the watched logs grow past
the size limit (a game stuck in a loop once filled a Linux disk with its log), the same fault line
repeats too often, or a stop pattern appears. Then everything the command started is killed, not
only the command: on Linux and macOS it runs in a process group of its own, and processes that left
the group (gdb puts the game in a group of its own) are followed by parent; on Windows it runs in a
Job Object (assigned before the process runs a single instruction). The script checks that nothing
of it is left and says so; it exits with the command's code, or:

    0  stopped after the stop pattern (--stop-on), as asked
  123  stopped at a repeated fault line (--repeat-pattern)
  124  stopped at the time limit
  125  stopped at the log size limit
  126  something of the command could not be killed (named in the report)
  130  the script itself was interrupted (SIGINT, SIGTERM, SIGHUP): the command is stopped first

Usage:
  tools/run_capped/run_capped.py [--timeout SECONDS] [--max-log-mb MB] [--watch PATH ...]
                                 [--output FILE] [--stop-on REGEX [--stop-delay SECONDS]]
                                 [--repeat-pattern REGEX] [--max-repeats N]
                                 [--head FILE] [--head-mb MB] -- COMMAND [ARGS ...]

  --watch    a file, or a folder whose files are each watched (recursively; only files written
             since the start count, and a file already there only by what it grows); repeatable.
             --output is always watched. Checked every 0.1 s: a log written at disk speed may pass
             a limit by what is written in that time.
  --output   where the command's stdout and stderr go (default: run_capped.out in the current
             folder).
  --max-log-mb  the bytes written to all watched files together during the run. Rotation is
             followed: a file renamed (log.txt to log.1.txt) is the same file, and the bytes of a
             part rotated away still count, so a log that rotates cannot hide a flood.
  --stop-on  a regular expression looked for in the lines written to the watched files; the run
             is stopped --stop-delay seconds after its first match.
  --repeat-pattern, --max-repeats  a line matching the pattern (the text it matches, from the
             match to the end of the line) seen more than N times stops the run. The default
             pattern is the SDK's "Unhandled guest access violation", which a game stuck on one
             bad pointer repeats thousands of times a second; default N is 100.
  --head     a copy of the first --head-mb MB (default 2) of the lines written to the watched
             files other than --output, in the order read, so the start of a run survives the
             rotation of its log (default: the --output path plus ".head"). A part written and
             rotated away between two checks (0.1 s) is counted but not read.

This replaces the agents' local run scripts.
"""

import argparse
import os
import pathlib
import re
import signal
import subprocess
import sys
import time

EXIT_STOP_PATTERN = 0
EXIT_REPEATED_FAULT = 123
EXIT_TIMEOUT = 124
EXIT_LOG_LIMIT = 125
EXIT_NOT_KILLED = 126
EXIT_INTERRUPTED = 130

POLL_SECONDS = 0.1
GRACE_SECONDS = 5.0


def process_table():
    """{pid: (parent pid, is a zombie)} of every process: /proc on Linux, ps elsewhere."""
    table = {}
    proc = pathlib.Path("/proc")
    if (proc / "self" / "stat").exists():
        for entry in proc.iterdir():
            if not entry.name.isdigit():
                continue
            try:
                stat = (entry / "stat").read_text()
            except OSError:
                continue
            fields = stat[stat.rindex(")") + 2:].split()
            table[int(entry.name)] = (int(fields[1]), fields[0] == "Z")
        return table
    out = subprocess.run(["ps", "-A", "-o", "pid=,ppid=,stat="], capture_output=True,
                         text=True).stdout
    for line in out.splitlines():
        fields = line.split()
        if len(fields) >= 3:
            table[int(fields[0])] = (int(fields[1]), fields[2].startswith("Z"))
    return table


class PosixGroup:
    """The command and everything it starts: its process group, plus every process seen below it
    (a child may leave the group, as gdb does with the program it runs)."""

    def __init__(self, command, output):
        self.process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
                                        stdin=subprocess.DEVNULL, start_new_session=True)
        self.group = self.process.pid  # a new session's leader: its pid is the group id
        self.known = set()  # processes seen below the command, whatever their group

    def follow(self):
        table = process_table()
        children = {}
        for pid, (ppid, _) in table.items():
            children.setdefault(ppid, []).append(pid)
        stack = [self.process.pid, *self.known]
        while stack:
            for child in children.get(stack.pop(), []):
                if child not in self.known:
                    self.known.add(child)
                    stack.append(child)

    def known_alive(self):
        table = process_table()
        return [pid for pid in self.known if pid in table and not table[pid][1]]

    def group_alive(self):
        try:
            os.killpg(self.group, 0)
            return True
        except ProcessLookupError:
            return False
        except PermissionError:
            return True  # something in the group belongs to another user: still there

    def alive(self):
        return self.group_alive() or bool(self.known_alive())

    def signal(self, kill):
        sig = signal.SIGKILL if kill else signal.SIGTERM
        try:
            os.killpg(self.group, sig)
        except ProcessLookupError:
            pass
        for pid in self.known_alive():
            try:
                os.kill(pid, sig)
            except ProcessLookupError:
                pass

    def reap(self):
        # The leader is our child: wait for it so it is not left a zombie, which keeps the group.
        try:
            self.process.wait(timeout=0)
        except subprocess.TimeoutExpired:
            pass

    def describe(self):
        left = sorted(self.known_alive())
        return f"process group {self.group}" + (f", processes {left}" if left else "")


class WindowsJob:
    """The command and everything it starts, in a Job Object that kills them all when closed."""

    def __init__(self, command, output):
        import ctypes
        from ctypes import wintypes
        self.ctypes, self.wintypes = ctypes, wintypes
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel32 = kernel32
        kernel32.CreateJobObjectW.restype = wintypes.HANDLE
        kernel32.CreateJobObjectW.argtypes = [wintypes.LPVOID, wintypes.LPCWSTR]
        kernel32.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int,
                                                      wintypes.LPVOID, wintypes.DWORD]
        kernel32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        kernel32.TerminateJobObject.argtypes = [wintypes.HANDLE, wintypes.UINT]
        kernel32.QueryInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int,
                                                        wintypes.LPVOID, wintypes.DWORD,
                                                        wintypes.LPVOID]
        self.job = kernel32.CreateJobObjectW(None, None)
        if not self.job:
            raise OSError(ctypes.get_last_error(), "CreateJobObjectW")

        class BasicLimit(ctypes.Structure):
            _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64),
                        ("PerJobUserTimeLimit", ctypes.c_int64),
                        ("LimitFlags", wintypes.DWORD),
                        ("MinimumWorkingSetSize", ctypes.c_size_t),
                        ("MaximumWorkingSetSize", ctypes.c_size_t),
                        ("ActiveProcessLimit", wintypes.DWORD),
                        ("Affinity", ctypes.c_size_t),
                        ("PriorityClass", wintypes.DWORD),
                        ("SchedulingClass", wintypes.DWORD)]

        class IoCounters(ctypes.Structure):
            _fields_ = [(name, ctypes.c_uint64) for name in
                        ("ReadOperationCount", "WriteOperationCount", "OtherOperationCount",
                         "ReadTransferCount", "WriteTransferCount", "OtherTransferCount")]

        class ExtendedLimit(ctypes.Structure):
            _fields_ = [("BasicLimitInformation", BasicLimit), ("IoInfo", IoCounters),
                        ("ProcessMemoryLimit", ctypes.c_size_t),
                        ("JobMemoryLimit", ctypes.c_size_t),
                        ("PeakProcessMemoryUsed", ctypes.c_size_t),
                        ("PeakJobMemoryUsed", ctypes.c_size_t)]

        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x2000
        JobObjectExtendedLimitInformation = 9
        limit = ExtendedLimit()
        limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not kernel32.SetInformationJobObject(self.job, JobObjectExtendedLimitInformation,
                                                ctypes.byref(limit), ctypes.sizeof(limit)):
            raise OSError(ctypes.get_last_error(), "SetInformationJobObject")
        # Suspended until it is in the job, so nothing it starts escapes it.
        CREATE_SUSPENDED = 0x4
        self.process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
                                        stdin=subprocess.DEVNULL,
                                        creationflags=CREATE_SUSPENDED)
        handle = wintypes.HANDLE(int(self.process._handle))
        if not kernel32.AssignProcessToJobObject(self.job, handle):
            error = ctypes.get_last_error()
            self.process.kill()
            raise OSError(error, "AssignProcessToJobObject")
        ntdll = ctypes.WinDLL("ntdll")
        ntdll.NtResumeProcess.argtypes = [wintypes.HANDLE]
        if ntdll.NtResumeProcess(handle) != 0:
            self.process.kill()
            raise OSError("NtResumeProcess failed")

    def active_processes(self):
        ctypes, wintypes = self.ctypes, self.wintypes

        class Accounting(ctypes.Structure):
            _fields_ = [("TotalUserTime", ctypes.c_int64), ("TotalKernelTime", ctypes.c_int64),
                        ("ThisPeriodTotalUserTime", ctypes.c_int64),
                        ("ThisPeriodTotalKernelTime", ctypes.c_int64),
                        ("TotalPageFaultCount", wintypes.DWORD),
                        ("TotalProcesses", wintypes.DWORD),
                        ("ActiveProcesses", wintypes.DWORD),
                        ("TotalTerminatedProcesses", wintypes.DWORD)]

        JobObjectBasicAccountingInformation = 1
        info = Accounting()
        if not self.kernel32.QueryInformationJobObject(self.job,
                                                       JobObjectBasicAccountingInformation,
                                                       ctypes.byref(info), ctypes.sizeof(info),
                                                       None):
            return 1  # cannot tell: assume something is left
        return info.ActiveProcesses

    def alive(self):
        return self.active_processes() > 0

    def signal(self, kill):
        # Windows has no gentle signal for a whole job: the first round terminates the command
        # itself (it may close its windows and files), the second the whole job.
        if not kill:
            self.process.terminate()
        else:
            self.kernel32.TerminateJobObject(self.job, 1)

    def reap(self):
        try:
            self.process.wait(timeout=0)
        except subprocess.TimeoutExpired:
            pass

    def describe(self):
        return f"job object ({self.active_processes()} process(es) active)"


def open_shared(path):
    """Opens a file to read without keeping its writer from renaming or deleting it. On Windows,
    Python's open() leaves out FILE_SHARE_DELETE, and a rotating log's rename then fails with a
    sharing violation while the file is open here (spdlog retries once, then throws)."""
    if os.name != "nt":
        return open(path, "rb")
    import ctypes
    import msvcrt
    from ctypes import wintypes
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateFileW.restype = wintypes.HANDLE
    kernel32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                                     wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    GENERIC_READ = 0x80000000
    FILE_SHARE_ALL = 0x1 | 0x2 | 0x4  # read, write, delete
    OPEN_EXISTING = 3
    handle = kernel32.CreateFileW(str(path), GENERIC_READ, FILE_SHARE_ALL, None, OPEN_EXISTING, 0, None)
    if handle in (None, wintypes.HANDLE(-1).value):
        raise OSError(ctypes.get_last_error(), "CreateFileW", str(path))
    try:
        fd = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
    except OSError:
        kernel32.CloseHandle(wintypes.HANDLE(handle))
        raise
    return os.fdopen(fd, "rb")


class LogWatcher:
    """The watched files written since the run started, read as they grow.

    A file is known by its identity (device and inode), not its name, so a log renamed by its
    rotation keeps its count and read position, and the bytes of a part deleted later still count.
    A file already there at the start counts and is read from its size then: what it held before
    the run is not this run's.
    """

    def __init__(self, paths, output_path, since, head_path, head_bytes):
        self.paths = paths
        self.output = output_path.resolve()
        self.since = since
        self.files = {}  # (device, inode) -> [read offset, size seen, unfinished last line]
        self.written = 0
        self.head_path, self.head_left, self.head, self.head_source = head_path, head_bytes, None, None
        for f in self.candidates():
            try:
                stat = f.stat()
            except OSError:
                continue
            self.files[(stat.st_dev, stat.st_ino)] = [stat.st_size, stat.st_size, b""]

    def candidates(self):
        for path in self.paths:
            if path.is_file():
                yield path
            elif path.is_dir():
                yield from (f for f in path.rglob("*") if f.is_file())

    def poll(self):
        """The lines finished since the last poll, as (path, line) pairs."""
        lines = []
        for f in self.candidates():
            try:
                stat = f.stat()
            except OSError:
                continue
            if stat.st_mtime < self.since:
                continue  # written before this run
            if self.head_path and f.resolve() == self.head_path.resolve():
                continue
            entry = self.files.setdefault((stat.st_dev, stat.st_ino), [0, 0, b""])
            if stat.st_size < entry[1]:  # truncated, or a new file reusing a deleted one's inode
                entry[:] = [0, 0, b""]
            self.written += stat.st_size - entry[1]
            entry[1] = stat.st_size
            if stat.st_size <= entry[0]:
                continue
            try:
                with open_shared(f) as handle:
                    handle.seek(entry[0])
                    data = handle.read(stat.st_size - entry[0])
            except OSError:
                continue
            entry[0] += len(data)
            parts = (entry[2] + data).split(b"\n")
            entry[2] = parts.pop()
            text = [p.decode(errors="replace") for p in parts]
            if f.resolve() != self.output:
                self.keep_head(f, text)
            lines += [(f, line) for line in text]
        return lines

    def keep_head(self, path, text):
        if not self.head_path or self.head_left <= 0:
            return
        if self.head is None:
            self.head = open(self.head_path, "w", encoding="utf-8")
        if path != self.head_source:
            self.head.write(f"== {path}\n")
            self.head_source = path
        for line in text:
            if self.head_left <= 0:
                self.head.write("== (head limit reached)\n")
                break
            self.head.write(line + "\n")
            self.head_left -= len(line) + 1
        self.head.flush()

    def close(self):
        if self.head is not None:
            self.head.close()


def run(command, timeout, max_log_bytes, watched, output_path, stop_on=None, stop_delay=0.0,
        repeat_pattern=None, max_repeats=0, head_path=None, head_bytes=0):
    start_wall = time.time()
    start = time.monotonic()
    logs = LogWatcher([output_path] + watched, output_path, start_wall, head_path, head_bytes)
    repeats = {}
    # An interrupted script must still stop the command: without this, Ctrl+C or a kill of the
    # script ends it at once and leaves the game running in its own session.
    interrupted = []
    for sig in [signal.SIGINT, signal.SIGTERM] + ([signal.SIGHUP] if hasattr(signal, "SIGHUP") else []):
        signal.signal(sig, lambda number, _frame: interrupted.append(number))
    stop_seen = None
    with open(output_path, "wb") as output:
        group = WindowsJob(command, output) if os.name == "nt" else PosixGroup(command, output)
        reason, code = None, None
        next_follow = 0.0
        while True:
            now = time.monotonic()
            if now >= next_follow and hasattr(group, "follow"):
                group.follow()
                next_follow = now + 1.0
            code = group.process.poll()
            if code is not None:
                reason = f"exited with code {code}"
                break
            if interrupted:
                reason, code = f"interrupted (signal {interrupted[0]})", EXIT_INTERRUPTED
                break
            if now - start > timeout:
                reason, code = f"time limit ({timeout:g} s)", EXIT_TIMEOUT
                break
            for path, line in logs.poll():
                if stop_on and stop_seen is None and stop_on.search(line):
                    stop_seen = now
                    print(f"run_capped: stop pattern seen in {path}; stopping in {stop_delay:g} s",
                          file=sys.stderr, flush=True)
                match = repeat_pattern.search(line) if repeat_pattern and max_repeats > 0 else None
                if match:
                    fault = line[match.start():]
                    repeats[fault] = repeats.get(fault, 0) + 1
                    if repeats[fault] > max_repeats and code is None:
                        reason = f"repeated more than {max_repeats} times: {fault}"
                        code = EXIT_REPEATED_FAULT
            if code is not None:
                break
            if logs.written > max_log_bytes:
                reason = f"log size limit: {logs.written / 2**20:.1f} MB written to the watched files"
                code = EXIT_LOG_LIMIT
                break
            if stop_seen is not None and now - stop_seen >= stop_delay:
                reason, code = "stop pattern seen", EXIT_STOP_PATTERN
                break
            time.sleep(POLL_SECONDS)

        # Whatever the reason, nothing the command started may outlive the run (a child it left
        # behind after exiting included).
        if hasattr(group, "follow"):
            group.follow()
        if group.alive():
            group.signal(kill=False)
            deadline = time.monotonic() + GRACE_SECONDS
            while group.alive() and time.monotonic() < deadline:
                group.reap()
                time.sleep(0.1)
            if group.alive():
                group.signal(kill=True)
                deadline = time.monotonic() + GRACE_SECONDS
                while group.alive() and time.monotonic() < deadline:
                    group.reap()
                    time.sleep(0.1)
        group.reap()
        left = group.alive()
    logs.poll()  # the last lines into the head copy
    logs.close()
    elapsed = time.monotonic() - start
    print(f"run_capped: {reason} after {elapsed:.1f} s; output in {output_path}", file=sys.stderr)
    if logs.head is not None:
        print(f"run_capped: start of the logs kept in {head_path}", file=sys.stderr)
    shm = pathlib.Path("/dev/shm")
    if shm.is_dir():  # the SDK's guest memory files on Linux, left by a run killed hard
        leftovers = len(list(shm.glob("xenia_memory_*")))
        print(f"run_capped: /dev/shm/xenia_memory_* files: {leftovers}", file=sys.stderr)
    if left:
        print(f"run_capped: NOT KILLED: {group.describe()} still has processes", file=sys.stderr)
        return EXIT_NOT_KILLED
    print("run_capped: nothing of the command is left", file=sys.stderr)
    return code


def main(argv):
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        usage="%(prog)s [options] -- COMMAND [ARGS ...]")
    parser.add_argument("--timeout", type=float, default=300.0, help="seconds (default 300)")
    parser.add_argument("--max-log-mb", type=float, default=20.0,
                        help="bytes all watched files may grow by together, in MB (default 20)")
    parser.add_argument("--watch", action="append", default=[], type=pathlib.Path,
                        help="a log file or folder to watch (repeatable)")
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("run_capped.out"),
                        help="the command's stdout and stderr (default run_capped.out)")
    parser.add_argument("--stop-on", type=re.compile, help="stop the run after this pattern")
    parser.add_argument("--stop-delay", type=float, default=0.0,
                        help="seconds to keep running after --stop-on matches (default 0)")
    parser.add_argument("--repeat-pattern", type=re.compile,
                        default=re.compile(r"Unhandled guest access violation"),
                        help="a fault line to count (default: the SDK's guest access violation)")
    parser.add_argument("--max-repeats", type=int, default=100,
                        help="times one fault line may repeat; 0 turns it off (default 100)")
    parser.add_argument("--head", type=pathlib.Path,
                        help="copy of the start of the logs (default: --output plus .head)")
    parser.add_argument("--head-mb", type=float, default=2.0,
                        help="how much of the logs' start to copy, in MB (default 2)")
    if "--" not in argv:
        parser.error("the command goes after --")
    split = argv.index("--")
    args = parser.parse_args(argv[:split])
    command = argv[split + 1:]
    if not command:
        parser.error("no command after --")
    head = args.head or args.output.with_name(args.output.name + ".head")
    return run(command, args.timeout, int(args.max_log_mb * 2**20), args.watch, args.output,
               args.stop_on, args.stop_delay, args.repeat_pattern, args.max_repeats, head,
               int(args.head_mb * 2**20))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
