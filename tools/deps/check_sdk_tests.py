#!/usr/bin/env python3
"""Checks a run of the ReXGlue SDK's tests from ctest's JUnit file (ctest --output-junit).

Every test has to run and pass. The only exception is a test skipped by its skip return code
(Catch2's SKIP) that tools/deps/sdk_test_skips.txt lists for this platform, with its reason, and
each listed test must have been skipped so: the list cannot go stale. A test that failed, that
ctest could not start, or that is missing from the run fails the check. Read from the XML, not
from ctest's text, whose wording and case change between versions and which test names can
contain too. Used by tools/deps/build_sdk.sh --tests and tools/build-deps/windows.ps1 -SdkTests.

Usage: tools/deps/check_sdk_tests.py JUNIT_XML PLATFORM [SKIPS_FILE]
  PLATFORM    linux, windows or macos (the first field of SKIPS_FILE's lines)
  SKIPS_FILE  default: sdk_test_skips.txt next to this script
Exit status: 0 when the run is as expected, 1 when not (each problem printed), 2 on bad input.
"""

import pathlib
import sys
import xml.etree.ElementTree as ET

PLATFORMS = ("linux", "windows", "macos")


def read_allowed(skips_file, platform):
    """The tests SKIPS_FILE lets this platform skip: {name: reason}."""
    allowed = {}
    for number, line in enumerate(pathlib.Path(skips_file).read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        fields = [field.strip() for field in line.split(" | ")]
        if len(fields) != 3 or fields[0] not in PLATFORMS or not fields[1] or not fields[2]:
            raise ValueError(f"{skips_file}:{number}: expected 'platform | test | reason'")
        if fields[0] == platform:
            allowed[fields[1]] = fields[2]
    return allowed


def check(junit_xml, platform, skips_file):
    """The problems of the run, as lines; none when it is as expected."""
    allowed = read_allowed(skips_file, platform)
    problems = []
    ran = skipped = 0
    seen = set()
    for case in ET.parse(junit_xml).getroot().iter("testcase"):
        name = case.get("name", "")
        status = case.get("status", "")
        seen.add(name)
        skip = case.find("skipped")
        skip_message = skip.get("message", "") if skip is not None else ""
        if status == "run":
            ran += 1
            if name in allowed:
                problems.append(f"listed as skipped on {platform}, but it ran: {name}")
        elif status == "notrun" and skip_message.startswith("SKIP_RETURN_CODE"):
            if name in allowed:
                skipped += 1
            else:
                problems.append(f"skipped, not listed for {platform}: {name}")
        elif status == "fail":
            problems.append(f"failed: {name}")
        else:
            problems.append(f"did not run ({status}: {skip_message or 'no reason'}): {name}")
    for name in sorted(set(allowed) - seen):
        problems.append(f"listed as skipped on {platform}, but not in the run: {name}")
    if not seen:
        problems.append("no tests in the run")
    print(f"SDK tests: {ran} ran, {skipped} skipped as listed for {platform}, "
          f"{len(problems)} problem(s)")
    return problems


def main(argv):
    if len(argv) not in (3, 4) or argv[2] not in PLATFORMS:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    skips_file = argv[3] if len(argv) == 4 else pathlib.Path(__file__).with_name(
        "sdk_test_skips.txt")
    try:
        problems = check(argv[1], argv[2], skips_file)
    except (OSError, ET.ParseError, ValueError) as error:
        print(f"check_sdk_tests: {error}", file=sys.stderr)
        return 2
    for problem in problems:
        print(f"  {problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
