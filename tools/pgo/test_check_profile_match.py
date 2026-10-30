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
    assert run(used + W.format(n=1000, m=10, v="have")) == 0, "1 % passes the 5 % limit"
    assert run(used + W.format(n=1000, m=200, v="have")) == 1, "20 % fails"
    assert run(used + W.format(n=1000, m=200, v="have"), "--max-mismatch-percent", "25") == 0
    assert run(used) == 0, "no mismatch reported: everything matched"
    assert run("clang++ -c a.cpp\n") == 1, "the profile was not used"
    assert run(used + W.format(n=10, m=5, v="have"), "--functions", "1000") == 0, "measured on all"
    print("check_profile_match: ok")


if __name__ == "__main__":
    main()
