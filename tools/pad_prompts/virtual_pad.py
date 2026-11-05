#!/usr/bin/env python3
"""A virtual gamepad for the pad prompts' live switch test (docs/mods.md 9b), Linux only.

Watches the game's log for the dev input script's captures (tools/input_scripts/pad-live-switch.txt):
after the first, creates a uinput gamepad with a real pad's USB IDs, which SDL types by them, and
presses R3 on it; after the second, removes it. The device is destroyed whatever happens (finally,
SIGINT, SIGTERM, timeout). Needs write access to /dev/uinput; no Python packages.

    tools/pad_prompts/virtual_pad.py LOG_FILE_OR_FOLDER [--kind ps5|ps4|switch] [--timeout 300]

LOG_FILE_OR_FOLDER: the game's log, or its folder (~/.local/state/TorchlightRecomp/logs, or
<root>/logs with --user_data_root: the
newest torchlight_N.log written after the start). Not run_capped's --output: the script's lines are
not on the game's stdout.
"""

import argparse
import ctypes
import fcntl
import os
import signal
import struct
import subprocess
import sys
import time

UI_SET_EVBIT = 0x40045564
UI_SET_KEYBIT = 0x40045565
UI_SET_ABSBIT = 0x40045567
UI_DEV_SETUP = 0x405C5503
UI_ABS_SETUP = 0x401C5504
UI_DEV_CREATE = 0x5501
UI_DEV_DESTROY = 0x5502
UI_GET_SYSNAME_64 = 0x8040552C  # _IOC(_IOC_READ, 'U', 44, 64)

EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
BUTTONS = [0x130, 0x131, 0x133, 0x134, 0x136, 0x137, 0x138, 0x139, 0x13A, 0x13B, 0x13C, 0x13D, 0x13E]
BTN_THUMBR = 0x13E
# (code, min, max, flat): sticks 0..255 as the real pad, triggers 0..255, the hat -1..1.
AXES = [(0, 0, 255, 0), (1, 0, 255, 0), (3, 0, 255, 0), (4, 0, 255, 0), (2, 0, 255, 0), (5, 0, 255, 0),
        (0x10, -1, 1, 0), (0x11, -1, 1, 0)]
# kind -> (vendor, product, version, name), as the real pads report them over USB.
KINDS = {
    "ps5": (0x054C, 0x0CE6, 0x8111, b"Sony Interactive Entertainment DualSense Wireless Controller"),
    "ps4": (0x054C, 0x09CC, 0x8111, b"Sony Interactive Entertainment Wireless Controller"),
    "switch": (0x057E, 0x2009, 0x8111, b"Nintendo Co., Ltd. Pro Controller"),
}


class VirtualPad:
    def __init__(self, kind):
        vendor, product, version, name = KINDS[kind]
        self.fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
        self.created = False
        fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_KEY)
        fcntl.ioctl(self.fd, UI_SET_EVBIT, EV_ABS)
        for code in BUTTONS:
            fcntl.ioctl(self.fd, UI_SET_KEYBIT, code)
        for code, lo, hi, flat in AXES:
            fcntl.ioctl(self.fd, UI_SET_ABSBIT, code)
            middle = (lo + hi) // 2
            fcntl.ioctl(self.fd, UI_ABS_SETUP, struct.pack("<HH6i", code, 0, middle, lo, hi, 0, flat, 0))
        setup = struct.pack("<4H80sI", 0x03, vendor, product, version, name, 0)
        fcntl.ioctl(self.fd, UI_DEV_SETUP, setup)
        fcntl.ioctl(self.fd, UI_DEV_CREATE)
        self.created = True
        buf = ctypes.create_string_buffer(64)
        fcntl.ioctl(self.fd, UI_GET_SYSNAME_64, buf)
        self.sysname = buf.value.decode()

    def emit(self, etype, code, value):
        now = time.time()
        os.write(self.fd, struct.pack("<qqHHi", int(now), int((now % 1) * 1e6), etype, code, value))

    def press(self, code):
        for value in (1, 0):
            self.emit(EV_KEY, code, value)
            self.emit(EV_SYN, 0, 0)
            time.sleep(0.15)

    def close(self):
        if self.created:
            try:
                fcntl.ioctl(self.fd, UI_DEV_DESTROY)
            finally:
                self.created = False
        os.close(self.fd)


def captures(log, since):
    """Captures logged so far; `log` a file, or the game's log folder (its newest torchlight_N.log
    written after `since`)."""
    if os.path.isdir(log):
        logs = [os.path.join(log, n) for n in os.listdir(log) if n.endswith(".log") and not n.endswith("_ogre.log")]
        logs = [p for p in logs if os.path.getmtime(p) >= since]
        if not logs:
            return 0
        log = max(logs, key=os.path.getmtime)
    try:
        with open(log, "rb") as f:
            return f.read().count(b"dev input script: capture")
    except OSError:
        return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("log")
    parser.add_argument("--kind", choices=sorted(KINDS), default="ps5")
    parser.add_argument("--timeout", type=float, default=300)
    args = parser.parse_args()

    def stop(signum, _frame):
        raise SystemExit(f"vpad: signal {signum}")

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    since = time.time()
    end = since + args.timeout
    pad = None
    try:
        while captures(args.log, since) < 1:
            if time.time() > end:
                raise SystemExit("vpad: timeout before the first capture")
            time.sleep(0.2)
        pad = VirtualPad(args.kind)
        print(f"vpad: created a {args.kind} pad, {pad.sysname}", flush=True)
        for path in sorted(os.listdir(f"/sys/devices/virtual/input/{pad.sysname}")):
            if path.startswith("event") or path.startswith("js"):
                node = f"/dev/input/{path}"
                info = subprocess.run(["udevadm", "info", "--query=property", node], capture_output=True, text=True)
                props = [l for l in info.stdout.splitlines() if l.startswith(("ID_INPUT", "ID_VENDOR_ID", "ID_MODEL_ID"))]
                print(f"vpad: {node} {' '.join(props)}", flush=True)
        time.sleep(2.0)
        pad.press(BTN_THUMBR)
        print("vpad: pressed R3", flush=True)
        while captures(args.log, since) < 2:
            if time.time() > end:
                raise SystemExit("vpad: timeout before the second capture")
            time.sleep(0.2)
    finally:
        if pad:
            pad.close()
            print("vpad: removed", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
