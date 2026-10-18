"""Summarises a D3D11 session's shader work from its two logs.

usage: analyze.py <torchlight_N.log> <torchlight_N_ogre.log>

- OGRE's log: every "RTSS: using <name> for Pass" line. A name seen for the first time in the
  session is a program OGRE creates and loads in it (D3DCompile on Direct3D 11, the microcode
  cache being off).
- The game's log: the backend's slow frame reports (over 25 ms). Each one carries
  "programs=N/Xms (rtss Yms)" for that frame: the draws that generated a program, timed whole,
  and the RTSS's generation part of it. Reports are capped per period; the period summaries say
  how many slow frames were not reported.
"""
import re
import sys

FRAME = re.compile(
    r"^\[(?P<t>[^\]]+)\].*live: slow frame by=(?P<by>\w+) \| (?P<cur>.*?) \| previous:")
FIELD = re.compile(r"(\w+)=([-\d.]+)")
PROGRAMS = re.compile(r"programs=(\d+)/([\d.]+)ms \(rtss ([\d.]+)ms\)")
TEXTURES = re.compile(r"textures=(\d+)/([\d.]+)ms")
SUMMARY = re.compile(r"slow frames \(over 25 ms\): (\d+) .*?, (\d+) not reported")
LOAD = re.compile(r"level load: (\d+) ms")
RTSS = re.compile(r"RTSS: using (\S+) for Pass")
PRECOMPILED = re.compile(r"live: (\d+) programs precompiled in (\d+) ms")
CACHE = re.compile(r"program cache[^\n]*")


def main(log_path, ogre_path):
    frames, slow_total, unreported, loads, precompiled, cache_lines = [], 0, 0, [], [], []
    with open(log_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = FRAME.search(line)
            if m:
                cur = m.group("cur")
                fields = dict(FIELD.findall(cur))
                p = PROGRAMS.search(cur)
                t = TEXTURES.search(cur)
                frames.append({
                    "time": m.group("t")[11:23], "by": m.group("by"),
                    "guest": float(fields.get("guest", 0)),
                    "backend": float(fields.get("backend", 0)),
                    "interval": float(fields.get("interval", 0)),
                    "programs": int(p.group(1)), "program_ms": float(p.group(2)),
                    "rtss_ms": float(p.group(3)),
                    "textures": int(t.group(1)), "texture_ms": float(t.group(2)),
                })
                continue
            m = SUMMARY.search(line)
            if m:
                slow_total += int(m.group(1))
                unreported += int(m.group(2))
                continue
            m = LOAD.search(line)
            if m:
                loads.append(int(m.group(1)))
                continue
            m = PRECOMPILED.search(line)
            if m:
                precompiled.append((line[12:24], int(m.group(1)), int(m.group(2))))
    uses, names = 0, set()
    with open(ogre_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = RTSS.search(line)
            if m:
                uses += 1
                names.add(m.group(1))
                continue
            m = CACHE.search(line)
            if m:
                cache_lines.append(line.strip())
    vs = sum(1 for n in names if n.endswith("_VS"))
    with_programs = [f for f in frames if f["programs"]]
    print(f"log: {log_path}")
    print(f"level loads (ms): {loads}")
    print(f"precompile lines: {len(precompiled)}, {sum(n for _, n, _ in precompiled)} programs, "
          f"{sum(ms for _, _, ms in precompiled)} ms")
    for t, n, ms in precompiled:
        print(f"  {t}  {n} programs precompiled in {ms} ms")
    print(f"OGRE log program cache lines: {len(cache_lines)}")
    for c in cache_lines:
        print(f"  {c}")
    print(f"OGRE log: {uses} RTSS pass uses, {len(names)} distinct programs created "
          f"({vs} vertex, {len(names) - vs} fragment)")
    print(f"slow frames: {slow_total} in the period summaries, {unreported} not reported, "
          f"{len(frames)} reported")
    print(f"reported slow frames with programs: {len(with_programs)}, "
          f"{sum(f['programs'] for f in with_programs)} program generations, "
          f"{sum(f['program_ms'] for f in with_programs):.1f} ms in their draws "
          f"(rtss {sum(f['rtss_ms'] for f in with_programs):.1f} ms)")
    if with_programs:
        per = sorted(f["program_ms"] / f["programs"] for f in with_programs)
        print(f"  per generated program, ms (draw time / programs): median "
              f"{per[len(per) // 2]:.2f}, max {per[-1]:.2f}")
    print("10 worst reported frames (by the larger of guest, backend, present interval):")
    print("  time          by       guest  backend  interval  programs/ms (rtss)   textures/ms")
    worst = sorted(frames, key=lambda f: -max(f["guest"], f["backend"], f["interval"]))[:10]
    for f in worst:
        print(f"  {f['time']}  {f['by']:<7} {f['guest']:7.1f}  {f['backend']:7.1f}  "
              f"{f['interval']:8.1f}  {f['programs']:3d}/{f['program_ms']:7.1f} "
              f"({f['rtss_ms']:6.1f})  {f['textures']:4d}/{f['texture_ms']:7.1f}")
    if frames:
        first = frames[0]["time"]
        def secs(s):
            h, m, rest = s.split(":")
            return int(h) * 3600 + int(m) * 60 + float(rest)
        early = [f for f in frames if secs(f["time"]) - secs(first) < 15 and f["programs"]]
        if early:
            w = max(early, key=lambda f: f["program_ms"])
            print(f"title screen (first 15 s of reports), frame with the most program time: "
                  f"{w['time']} guest {w['guest']:.1f} backend {w['backend']:.1f} "
                  f"programs {w['programs']}/{w['program_ms']:.1f} ms")
        else:
            print("title screen (first 15 s of reports): no reported frame with programs")
    print("10 reported frames with the most program time:")
    for f in sorted(with_programs, key=lambda f: -f["program_ms"])[:10]:
        print(f"  {f['time']}  {f['by']:<7} guest {f['guest']:7.1f}  backend {f['backend']:7.1f}  "
              f"programs {f['programs']}/{f['program_ms']:.1f} ms (rtss {f['rtss_ms']:.1f})")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
