#!/usr/bin/env python3
"""Runs the game (or the game under gdb) so that it can never be left running.

The command is started in its own process group and session. The run is stopped when:
- the hard time limit passes (exit 2);
- the run's logs grow past a size limit (exit 3): the command's own output plus the game's log
  and its rotated parts (the game's first stdout line names it: "torchlight: log <path>");
- a pattern appears in the game's log, after a further delay (--stop-on, --stop-delay; exit 0);
- the script itself is interrupted (exit 130).
If the command ends on its own, its exit code is returned.

Stopping signals SIGTERM, then SIGKILL, to the whole group and to every descendant (a wrapper or
gdb may move the game elsewhere). Then it checks that none of them is left. If one is, it says so
and exits 4. It also counts leftover /dev/shm/xenia_memory_* files.

Example:
  tools/run_capped.py --timeout 180 --max-log-mb 20 --run-log out/validation/x/run.log \\
      --stop-on "loading the unit index" --stop-delay 20 -- \\
      env XDG_CONFIG_HOME=... out/build/relwithdebinfo/torchlight --capture_dir ...
"""

import argparse
import os
import re
import signal
import subprocess
import sys
import time
from pathlib import Path

POLL_SECONDS = 0.5
TERM_GRACE_SECONDS = 5
KILL_GRACE_SECONDS = 5


def descendants(root):
    """Every live process below `root`, from /proc."""
    children = {}
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            stat = (entry / "stat").read_text()
        except OSError:
            continue
        ppid = int(stat[stat.rindex(")") + 2:].split()[1])
        children.setdefault(ppid, []).append(int(entry.name))
    out, stack = [], [root]
    while stack:
        for child in children.get(stack.pop(), []):
            out.append(child)
            stack.append(child)
    return out


def group_alive(pgid):
    try:
        os.killpg(pgid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def pid_alive(pid):
    try:
        stat = Path(f"/proc/{pid}/stat").read_text()
    except OSError:
        return False
    return stat[stat.rindex(")") + 2] != "Z"


def signal_all(pgid, pids, sig):
    try:
        os.killpg(pgid, sig)
    except ProcessLookupError:
        pass
    for pid in pids:
        try:
            os.kill(pid, sig)
        except ProcessLookupError:
            pass


def stop(process, known):
    """Stops the group and every process seen below it; True when none is left."""
    pgid = process.pid
    pids = set(known) | set(descendants(process.pid)) | {process.pid}
    for sig, grace in ((signal.SIGTERM, TERM_GRACE_SECONDS), (signal.SIGKILL, KILL_GRACE_SECONDS)):
        signal_all(pgid, pids, sig)
        deadline = time.monotonic() + grace
        while time.monotonic() < deadline:
            process.poll()
            if not group_alive(pgid) and not any(pid_alive(p) for p in pids):
                return True
            time.sleep(0.2)
    process.poll()
    left = [p for p in pids if pid_alive(p)]
    if left or group_alive(pgid):
        print(f"run_capped: STILL RUNNING after SIGKILL: group {pgid}, pids {left}", file=sys.stderr)
        return False
    return True


def game_log(run_log):
    try:
        text = run_log.read_text(errors="replace")
    except OSError:
        return None
    match = re.search(r"^torchlight: log (.+)$", text, re.MULTILINE)
    return Path(match.group(1).strip()) if match else None


def logs_size(run_log, log):
    paths = [run_log]
    if log:
        paths.append(log)
        paths += log.parent.glob(log.stem + ".*.log")  # rotated parts: <stem>.1.log, ...
    total = 0
    for path in paths:
        try:
            total += path.stat().st_size
        except OSError:
            pass
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--timeout", type=float, required=True, help="hard limit in seconds")
    parser.add_argument("--max-log-mb", type=float, default=20)
    parser.add_argument("--run-log", type=Path, required=True, help="the command's stdout and stderr")
    parser.add_argument("--stop-on", help="regular expression looked for in the game's log")
    parser.add_argument("--stop-delay", type=float, default=0, help="seconds to keep running after it")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("no command")

    args.run_log.parent.mkdir(parents=True, exist_ok=True)
    with open(args.run_log, "wb") as out:
        process = subprocess.Popen(command, stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                   start_new_session=True)
    print(f"run_capped: started group {process.pid}", flush=True)

    interrupted = []
    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, lambda *_: interrupted.append(True))

    start = time.monotonic()
    limit = args.max_log_mb * 1024 * 1024
    pattern = re.compile(args.stop_on) if args.stop_on else None
    known, log, seen_at, offset = set(), None, None, 0
    reason, code = None, 0
    while True:
        if process.poll() is not None:
            reason, code = f"the command ended with {process.returncode}", process.returncode
            break
        known.update(descendants(process.pid))
        log = log or game_log(args.run_log)
        if interrupted:
            reason, code = "interrupted", 130
            break
        if time.monotonic() - start > args.timeout:
            reason, code = f"time limit ({args.timeout:.0f} s)", 2
            break
        size = logs_size(args.run_log, log)
        if size > limit:
            reason, code = f"logs at {size / 1048576:.1f} MB, over {args.max_log_mb:g} MB", 3
            break
        if pattern and log and seen_at is None:
            try:
                with open(log, "rb") as f:
                    f.seek(offset)
                    chunk = f.read()
                if pattern.search(chunk.decode(errors="replace")):
                    seen_at = time.monotonic()
                    print(f"run_capped: \"{args.stop_on}\" seen; stopping in {args.stop_delay:g} s", flush=True)
                offset += max(0, len(chunk) - 4096)  # keep an overlap for a line cut in two
            except OSError:
                pass
        if seen_at is not None and time.monotonic() - seen_at >= args.stop_delay:
            reason, code = "stop pattern seen", 0
            break
        time.sleep(POLL_SECONDS)

    print(f"run_capped: stopping: {reason}", flush=True)
    clean = stop(process, known)
    shm = [p.name for p in Path("/dev/shm").glob("xenia_memory_*")]
    print(f"run_capped: {'all processes gone' if clean else 'PROCESSES LEFT'}; game log {log}; "
          f"/dev/shm leftovers: {len(shm)}", flush=True)
    return code if clean else 4


if __name__ == "__main__":
    sys.exit(main())
