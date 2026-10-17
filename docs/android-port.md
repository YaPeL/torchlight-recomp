# Android port: notes

Studies for a port to Android (the first target in mind: a Samsung Galaxy Tab S5e, Snapdragon 670
with an Adreno 615, OpenGL ES 3.2 and Vulkan 1.1, 4-6 GB). Nothing here is implemented.

## The plan as discussed (2026-10-16)

- **Render system:** OGRE 14's GLES2 render system (OpenGL ES 3.x; the RTSS writes GLSL ES) or
  Vulkan, with the conventions reviewed per render system as for Direct3D 11 (WIN.7).
- **ARM64:** the code generator and the runtime already run on ARM64 (the macOS port).
- **The SDK:** a ReXGlue game already ships on Android (`edeegg/origins-recomp`,
  `docs/installer-research.md`). Android is a Linux kernel (mmap, signals, pages); `shm_open`,
  which bionic lacks, would become `memfd`. The package (APK), the app's life cycle and input (a
  Bluetooth pad through SDL; touch controls without one) come with it.
- **DXT textures:** Adreno GPUs generally lack S3TC, and the game's textures are mostly DXT. The
  proposal: transcode to ETC2 (guaranteed by OpenGL ES 3.0, the same size as DXT) on a texture's
  first use, kept in a cache on disk keyed by the source file's hash, the cache filled from the
  pak at install or in the background on the first start; RGBA until a texture is in the cache.
  All in the backend, decided by the render system's capability; the guest and the captures keep
  DXT.
- **CPU:** two fast cores (Kryo 360 Gold, A75-derived, 2 GHz), a rough 4-5 times slower per core
  than the Mac's M2: about 12-15 ms of game thread per frame, 60 in menus, possibly below it in
  fights. A Raspberry Pi 5 (Linux ARM64) could measure the game thread first.

## Textures the game creates at runtime, not from a file (measured 2026-10-17)

The transcoding cache covers textures read from files (pak, mods, replacements). The question
here was the others: the textures the guest creates at runtime and fills itself, which a cache
keyed by file cannot hold. Whether any is DXT, how many, how large and how often, to decide
between RGBA on the fly and ETC2 encoding on a worker thread.

**Method.** A temporary counter in the frontend (local branch, not in the repository) recorded
every texture taken through the dynamic path (no file: content captured from the guest's
memory): format after detiling, size, the guest's mip count, every upload with its time. Linux,
develop after v0.12.5-beta, two automatic runs on copies of the fixed-floor saved game (no mods,
and Enhanced Edition with its two companion mods): mine floor 1 with a fight (god mode), the
inventory and its tabs, the Y, START and LT menus, five floors down with a fight on each, six
floors up to the town, 18 level loads in about 3 minutes each. Not covered for certain: whether
any of those buttons opened the full map, and a large fight (the fights were small groups).

**Result.**

| | No mods | Enhanced Edition |
|---|---|---|
| Textures created at runtime | 26 | 26 |
| Of them DXT | **0** | **0** |
| Size in all (A8R8G8B8) | 8.9 MiB | 8.7 MiB |
| Uploads in all | 41 | 26 |
| Uploaded more than once | 1 (128 x 128, 16 times within 0.7 s, once) | 0 |

- Every one is uncompressed A8R8G8B8: the five CEGUI glyph sheets (one of 512 x 512, four of
  256 x 256, 2 MiB in all, created at start and at the first use of a font), and one to three
  per level load (512 x 256, 512 x 128, 256 x 256 or 128 x 128, with mips), created during the
  load and not touched afterwards.
- Only one texture was written again during play, 16 times in 0.7 s once in the run without mods;
  nothing is regenerated on a regular rhythm.

**Conclusion.** No texture the game creates at runtime needs DXT handling: they are already RGBA,
about 9 MiB in all, and created during loads. The Android port's DXT work is the files' textures
only, as the plan above has it (the ETC2 cache, RGBA until cached); RGBA on the fly is enough for
the runtime ones, with nothing to encode on a worker thread for them. To confirm with the full map
and a large fight before the port starts.
