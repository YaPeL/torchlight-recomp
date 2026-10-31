#!/usr/bin/env python3
"""check_profile_match.py over made-up build logs."""

import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_profile_match as c  # noqa: E402

W = ("x.cpp:1:1: warning: profile data may be out of date: of {n} functions, {m} {v} mismatched "
     "data that will be ignored [-Wprofile-instr-out-of-date]\n")


def run(text, *extra):
    with tempfile.NamedTemporaryFile("w", suffix=".log", delete=False) as f:
        f.write(text)
    try:
        return c.main([f.name, *extra])
    finally:
        os.unlink(f.name)


def main():
    used = "clang++ -fprofile-instr-use=/p.profdata -c a.cpp\n"
    assert c.mismatch(W.format(n=100, m=1, v="has") + W.format(n=50, m=4, v="have")) == (150, 5, 2)
    f = ("--functions", "1000")
    assert run(used + W.format(n=1000, m=10, v="have"), *f) == 0, "1 % passes the 5 % limit"
    assert run(used + W.format(n=1000, m=200, v="have"), *f) == 1, "20 % fails"
    assert run(used + W.format(n=1000, m=200, v="have"), *f, "--max-mismatch-percent", "25") == 0
    assert run(used + W.format(n=1000, m=10, v="have")) == 1, "a mismatch needs the profile count"
    assert run(used) == 0, "no mismatch reported: everything matched"
    assert run("clang++ -c a.cpp\n") == 1, "the profile was not used"
    assert run(used + W.format(n=40000, m=1500, v="have"), "--functions", "20000") == 1, \
        "measured on the profile, not on the units that warned"
    d = tempfile.mkdtemp()
    with open(os.path.join(d, "build.ninja"), "w") as f:
        f.write("FLAGS = -O3 -fprofile-instr-use=/p.profdata\n")
    assert run("[1/2] Building CXX object a.o\n", "--build-dir", d) == 0, "used per build.ninja"
    # A stand-in for llvm-profdata show (portable: no script to execute).
    real_run = c.subprocess.run

    def fake_run(cmd, **kwargs):
        if cmd[0] == "missing":
            raise FileNotFoundError(cmd[0])
        return c.subprocess.CompletedProcess(
            cmd, 0, "Instrumentation level: Front-end\nTotal functions: 400\n", "")

    c.subprocess.run = fake_run
    tool = "fake-profdata"
    assert run(used + W.format(n=100, m=30, v="have"), "--profdata", tool) == 1, "30 of 400 fails"
    assert run(used + W.format(n=100, m=10, v="have"), "--profdata", tool) == 0, "10 of 400 passes"
    assert run("[1/2] Building CXX object a.o\n", "--build-dir", d, "--profdata", tool) == 0
    assert run(used, "--profdata", "missing") == 1, "no count: fails"
    c.subprocess.run = real_run
    print("check_profile_match: ok")


if __name__ == "__main__":
    main()
