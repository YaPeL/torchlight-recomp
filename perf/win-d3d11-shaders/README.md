# Direct3D 11: are shaders compiled in every session?

Windows, 2026-10-17. Develop a2fa06b, Release, built from scratch (SDK D:\rexglue-sdk-33, ctest
102/102). RTX 5080, 3840x2160, D3D11 (automatic render system). Isolated profile
(`--user_data_root`, a copy of the plain test save, no mods). Two sessions in a row with the same
route: start, Continue, town, into the mine, one floor, quit from the menu. Nothing deleted between
them.

## Answer

Yes. The second session compiles again: 156 distinct programs were created and loaded in it, 154
of them already created in the first one. OGRE's microcode cache is off, and the RTSS's cache only
keeps the generated HLSL text. The driver's cache cannot help here, because D3DCompile (HLSL to
DXBC) runs on the CPU before the driver sees anything.

In the second session each program is cheaper. The draws that generated a program cost a median of
9.7 ms per program, against 22.7 ms in the first. Still, every session pays it.

| | Session 1 | Session 2 |
|---|---|---|
| Level loads (ms) | 5053, 2978, 5542 | 5078, 3306, 5084 |
| Distinct programs created (OGRE log) | 189 (58 VS, 131 FS) | 156 (52 VS, 104 FS) |
| Of them also in the other session | 154 | 154 |
| RTSS sources in the cache folder | 212 before, 215 after | 215 before, 215 after |
| Slow frames (over 25 ms) | 422 (324 not reported) | 386 (253 not reported) |
| Reported slow frames with programs | 6 | 7 |
| Program generations in them | 18 | 31 |
| Their draws' time | 404.6 ms (RTSS part 402.1) | 253.2 ms (RTSS part 250.5) |
| Per program, median / max | 22.65 / 26.57 ms | 9.69 / 13.10 ms |

The slow-frame reports are capped per period, and a frame under 25 ms is never reported. So these
totals are only the part that landed in reported frames, not the session's whole compile time.
For a rough order: 156 programs at about 10 ms each is about 1.5 s per session, most of it inside
loading screens.

In the "RTSS part", the RTSS generates the source and loads the program, which runs D3DCompile on
D3D11. From the counters, compiling cannot be separated from generating without a new timer,
which was not added.

The 10 worst reported frames of each session are in `session-N-summary.txt`. Most are guest-side
frames with no program: loading screens. One is the 5 s autosave notice in session 2 (22:51:29,
5 s after the save at 22:51:24.7). The frame with the most program time is a title screen frame
2-7 s after start: 10 programs, 226 ms in session 1 and 131 ms in session 2. The others are
during level loads or in the first frames after one.

## What the code does (read, not traced)

- **Microcode cache:**
  - `GpuProgramManager::mSaveMicrocodesToCache` starts false (`OgreGpuProgramManager.cpp:133`).
  - Nothing in `src/` calls `setSaveMicrocodesToCache`, `loadMicrocodeCache` or
    `saveMicrocodeCache`.
  - `D3D11HLSLProgram::loadFromSource` takes a program from the cache only if it is there
    (`OgreD3D11HLSLProgram.cpp:66-73`), and adds compiled programs to it only when saving is on
    (`:510`).
  - OGRE writes no cache file of its own: `saveMicrocodeCache` and `loadMicrocodeCache` take a
    stream that the application opens (`OgreGpuProgramManager.cpp:341, 376`). So today there is no
    file and no path. If it were turned on, the natural place is the same folder as the RTSS's
    sources, `platform::ShaderCacheDir()`. The device reports "Read-back compiled shader: yes", so
    `canGetCompiledShaderBuffer()` would allow it.
- **RTSS cache:**
  - `ShaderGenerator::setShaderCachePath(platform::ShaderCacheDir())` (`src/backend/backend.cpp:1714`).
  - The cache folder is `%LOCALAPPDATA%\TorchlightRecomp\ogre`, because the cache kind is not
    under `--user_data_root`.
  - The RTSS always writes the source first, to hash it into the program's name. With a cache path
    it then reads the same text back from the file, if there is one, and loads it
    (`OgreShaderProgramManager.cpp:165-249`).
  - So the RTSS cache saves neither the generation nor the compilation.
- **ARCHITECTURE.md:** in its shader compilation section it says the decision to have no cache of
  our own relies on the driver's cache "on Windows" too. That holds for GL3+, where the driver
  compiles GLSL and caches it. It does not hold for D3D11, where HLSL to DXBC is D3DCompile in the
  application.
- **Why session 2 is cheaper per program:** the reason is not measured. Possibly D3DCompile's DLL
  and the source files are already warm in memory, and the driver's own cache of the later step,
  DXBC to GPU code, also helps.

## Files

- `analyze.py`: the summary script (`python analyze.py torchlight_N.log torchlight_N_ogre.log`).
- `session-N-summary.txt`: its output for each session.
- `session-N-log.txt`: from the game's log, the build line, level loads, slow-frame period
  summaries, the reported slow frames that generated programs (whole lines, with the previous
  frame), and the exit.
- `session-N-ogre-log.txt`: from OGRE's log, the render system and shader profile lines, and every
  `RTSS: using <program> for Pass` line.
