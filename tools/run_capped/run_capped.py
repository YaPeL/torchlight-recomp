#!/usr/bin/env python3
"""Runs the game (or any command) with hard limits, for every agent and platform.

A run ends at the first of: the command exits, the time limit passes, or a watched log grows past
the size limit (a game stuck in a loop once filled a Linux disk with its log). Then everything the
command started is killed, not only the command: on Linux and macOS it runs in a process group of
its own, on Windows in a Job Object (assigned before the process runs a single instruction). The
script checks that nothing of it is left and says so; it exits with the command's code, or:

  124  stopped at the time limit
  125  stopped at the log size limit
  126  something of the command could not be killed (named in the report)

Usage:
  tools/run_capped/run_capped.py [--timeout SECONDS] [--max-log-mb MB] [--watch PATH ...]
                                 [--output FILE] -- COMMAND [ARGS ...]

  --watch    a file, or a folder whose files are each watched (recursively; only files written
             since the start count); repeatable. --output is always watched. Checked every 0.1 s:
             a log written at disk speed may pass the limit by what is written in that time.
  --output   where the command's stdout and stderr go (default: run_capped.out in the current
             folder).

This replaces the agents' local run scripts (docs/macos-port.md, MAC.6).
"""

import argparse
import os
import pathlib
import signal
import subprocess
import sys
import time

EXIT_TIMEOUT = 124
EXIT_LOG_LIMIT = 125
EXIT_NOT_KILLED = 126

POLL_SECONDS = 0.1
GRACE_SECONDS = 5.0


class PosixGroup:
    """The command and everything it starts, as one process group."""

    def __init__(self, command, output):
        self.process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
                                        stdin=subprocess.DEVNULL, start_new_session=True)
        self.group = self.process.pid  # a new session's leader: its pid is the group id

    def alive(self):
        try:
            os.killpg(self.group, 0)
            return True
        except ProcessLookupError:
            return False
        except PermissionError:
            return True  # something in the group belongs to another user: still there

    def signal(self, kill):
        try:
            os.killpg(self.group, signal.SIGKILL if kill else signal.SIGTERM)
        except ProcessLookupError:
            pass

    def reap(self):
        # The leader is our child: wait for it so it is not left a zombie, which keeps the group.
        try:
            self.process.wait(timeout=0)
        except subprocess.TimeoutExpired:
            pass

    def describe(self):
        return f"process group {self.group}"


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


def largest_log(paths, since):
    """The largest watched file written since `since`, as (path, bytes); (None, 0) without one."""
    largest = (None, 0)
    for path in paths:
        if path.is_file():
            files = [path]
        elif path.is_dir():
            files = [f for f in path.rglob("*") if f.is_file()]
        else:
            continue
        for f in files:
            try:
                stat = f.stat()
            except OSError:
                continue
            if stat.st_mtime < since:
                continue  # written before this run
            if stat.st_size > largest[1]:
                largest = (f, stat.st_size)
    return largest


def run(command, timeout, max_log_bytes, watched, output_path):
    start_wall = time.time()
    start = time.monotonic()
    with open(output_path, "wb") as output:
        group = WindowsJob(command, output) if os.name == "nt" else PosixGroup(command, output)
        reason, code = None, None
        paths = [output_path] + watched
        while True:
            code = group.process.poll()
            if code is not None:
                reason = f"exited with code {code}"
                break
            if time.monotonic() - start > timeout:
                reason, code = f"time limit ({timeout:g} s)", EXIT_TIMEOUT
                break
            log, size = largest_log(paths, start_wall)
            if size > max_log_bytes:
                reason = f"log size limit: {log} has {size / 2**20:.1f} MB"
                code = EXIT_LOG_LIMIT
                break
            time.sleep(POLL_SECONDS)

        # Whatever the reason, nothing the command started may outlive the run (a child it left
        # behind after exiting included).
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
    elapsed = time.monotonic() - start
    print(f"run_capped: {reason} after {elapsed:.1f} s; output in {output_path}", file=sys.stderr)
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
                        help="largest a watched file may grow, in MB (default 20)")
    parser.add_argument("--watch", action="append", default=[], type=pathlib.Path,
                        help="a log file or folder to watch (repeatable)")
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("run_capped.out"),
                        help="the command's stdout and stderr (default run_capped.out)")
    if "--" not in argv:
        parser.error("the command goes after --")
    split = argv.index("--")
    args = parser.parse_args(argv[:split])
    command = argv[split + 1:]
    if not command:
        parser.error("no command after --")
    return run(command, args.timeout, int(args.max_log_mb * 2**20), args.watch, args.output)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
