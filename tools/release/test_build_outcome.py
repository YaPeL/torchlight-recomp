#!/usr/bin/env python3
"""build_outcome.py on a made-up build log: what it prints, and that paths, command lines and
source lines stay out."""

import importlib.util
import pathlib
import sys

spec = importlib.util.spec_from_file_location(
    "build_outcome", pathlib.Path(__file__).with_name("build_outcome.py"))
build_outcome = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_outcome)

LOG = """[1/3] Building CXX object CMakeFiles/a.dir/generated/default/torchlight_recomp.1.cpp.o
/runner/work/x/generated/default/torchlight_recomp.1.cpp: warning: profile data may be out of date: of 10 functions, 1 has mismatched data that will be ignored [-Wprofile-instr-out-of-date]
FAILED: CMakeFiles/a.dir/src/b.cpp.o
/usr/bin/clang++ -fprofile-instr-use=/secret/place/linux.profdata -c /runner/work/x/src/b.cpp
/runner/work/x/src/b.cpp:12:3: error: use of undeclared identifier 'q'
   12 |   q = 1;
      |   ^
[3/3] Linking CXX executable torchlight
/runner/work/x/out/build/linux-amd64-release/torchlight: 43927 guest functions, 209 hooked, 392796 direct branches to guest functions
bypass: 1
  OUTLINED_FUNCTION_7 at 1021ccba8: b to __imp__sub_8276A858, past the hook sub_8276A858
ninja: build stopped: subcommand failed.
"""

WANT = [
    "torchlight: 43927 guest functions, 209 hooked, 392796 direct branches to guest functions",
    "bypass: 1",
    "  OUTLINED_FUNCTION_7 at 1021ccba8: b to __imp__sub_8276A858, past the hook sub_8276A858",
    "1 translation units reported mismatched profile data",
    "FAILED: b.cpp.o",
    "b.cpp: error: use of undeclared identifier 'q'",
    "[3/3] (ninja's last step)",
]

got = build_outcome.outcome(LOG.splitlines(True))
failures = []
if got != WANT:
    failures.append("got:\n  " + "\n  ".join(got))
text = "\n".join(got)
for leak in ("/runner", "/secret", "profdata", "q = 1"):
    if leak in text:
        failures.append(f"{leak!r} printed")
for failure in failures:
    print(f"build_outcome_test: {failure}")
sys.exit(1 if failures else 0)
