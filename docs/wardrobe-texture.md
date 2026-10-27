# The equipment texture build (CWardrobe) and the host colour pack/unpack

What the game does on the CPU each time a character's equipment changes, why that is the equip
hitch, and the host path that shortens it (`src/hooks/pixel_util.*`). Addresses are the Xbox 360
build's; the algorithm is the one to reproduce on another build of the game (the PC version
included).

## Where the time goes

Measured on the desktop (Release build, frame pointers, `perf record -F 999 --call-graph fp`), a
new Destroyer on the mine's first floor picking up pieces of armour that equip themselves:

- each such pickup had a 64-74 ms guest frame, against about 7.5 ms around it;
- 47-58 ms of it under the pickup handler (`sub_822960D0` -> `sub_8221B3A8` -> `sub_82295D20` ->
  `sub_822E1D98` -> `sub_822A0B28`), and about 44 ms of that in one function, the wardrobe build
  `sub_822DC6B0` (`guest_abi/wardrobe.h` `kBuild`);
- inside it, per build (four builds per run, two runs each way, `perf` at 999 Hz, the samples
  inclusive in each guest function): about 13 ms in `PixelUtil::unpackColour` and 6 ms in
  `packColour` (called for every pixel of each item's texture, below), about 15 ms decoding the
  item's texture file into an image (`sub_82471380`), the rest the loop itself, meshes, materials
  and entities;
- the frame's rendering is hardly longer; the host's own code is under 4 % of it.

With the host colour pack/unpack below (`--native_pixel_util`, default on) the two take about
10 ms and 3.5 ms per build: the wardrobe build goes from about 41 ms to about 37 ms (A B B A runs,
156 and 176 samples for four builds without, 149 and 148 with). The recompiled originals were
already about 30 ns per call; most of what is left is the call itself.

The cost is the same at every equip: it is work the game redoes, not something loaded the first
time.

## The texture build, step by step

`sub_822DC6B0(r3 = the CWardrobe, r4 = a flag)`; the rebuild path, when the wardrobe's texture
(+56) and its pixel buffer (+68) exist (`@0x822DC930`):

1. The buffer's width and height (+68 -> +4, +8; `@0x822DC9A0`), the texture's pixel format (+56,
   vtable +292, `@0x822DC944`).
2. A CPU copy of the texture as it is now ("the base"): a 44-byte `Ogre::Image` (vtable
   `0x820DF2EC`) allocated with the game's `operator new` (`sub_821CD7F8`), a width x height x
   bytes-per-pixel block, `Image::loadDynamicImage` (`sub_82471258`), then the texture's buffer
   blitted into it (`sub_82474C88`, `@0x822DCAA4`). Its pixel box is copied to the stack
   (`@0x822DCAC4`).
3. For each of the five equipment slots (`r16` = 0..4, `@0x822DCB50` .. `@0x822DD3C8`):
   - the slot's item (`sub_821D5380`); in its unit data, the `WARDROBE` entries; the first whose
     `CLASS` matches the character's class (`[character+388]+48`) gives `MESH` and `TEXTURE`
     (`sub_821A1510` finds a key, `sub_821A1378` reads it: fewer than ten lookups per build);
   - the mesh: `sub_8238FB60` (by name, through the scene manager at +8);
   - when the slot has a `TEXTURE`: the file opened from the resource groups (`sub_82204410`,
     `sub_8239D5F8`), decoded into a new `Ogre::Image` (`sub_82471380`, `@0x822DD0E4`), and blended
     into the base (below), then that image deleted.
4. The base written back to the texture (`@0x822DD3D8` on).

## The blend, exactly

For the item image A (data pointer from its pixel box, format `[A+28]`) and the base B (data
`[pixelbox+24]`, the texture's format), both stepped 4 bytes per pixel, `rows` x `columns` of the
texture (`@0x822DD1D8` .. `@0x822DD394`):

```
for each row, for each column:
    unpackColour(&ar, &ag, &ab, &aa, formatA, A)          // 8-bit channels (kUnpackColour8)
    if aa >= 255:
        packColour(ar, ag, ab, aa, formatB, B)            // the item's pixel as it is
    else:
        unpackColour(&br, &bg, &bb, &ba, formatB, B)
        alpha = float(aa) * (1/255)                       // single precision; 1/255 = pool 0x820AA2C0+92
        inv   = 1.0f - alpha                              // pool +64
        r = fma(float(br), inv, float(ar) * alpha)        // fmadds: one rounding
        g = fma(float(bg), inv, float(ag) * alpha)
        b = fma(float(bb), inv, float(ab) * alpha)
        packColour(byte(trunc(r)), byte(trunc(g)), byte(trunc(b)), 255, formatB, B)
    A += 4; B += 4
```

- Every product and sum is single precision (`fmuls`, `fsubs`); the three blends are fused
  multiply-adds (`fmadds`). The recompiled code computes each in double and rounds it to single
  (`double(float(std::fma(...)))`), which the host loop repeats; a port to hardware with a
  single-precision fused multiply-add (`fmaf`) does what the console does. Both products are of
  floats, exact in double, so the two differ only in rare double roundings; the checks below are
  what proves a port.
- `trunc` is `fctidz` (towards zero, to a 64-bit integer) and `byte` its lowest byte
  (`lbz +199`): the value modulo 256, not a clamp (the blend stays in 0..255 anyway).
- The blended pixel's alpha is always 255; an opaque item pixel keeps its own alpha (255).
- The loop assumes 4-byte pixels in both images (the texture formats the game uses here are
  32-bit).

## The host colour pack/unpack

`unpackColour` (8-bit, `sub_82474560`) and `packColour` (8-bit, `sub_82473FA0`) are
`Ogre::PixelUtil`'s as OGRE 1.7 writes them, over the guest's copy of OGRE's pixel format table
(`0x83065EB0`, 42 entries of 44 bytes; `guest_abi/pixel_util.h` has every field with its evidence).
Both are pure: they read the table and the pixel or the four channels, and write only their outputs
(the four bytes, or the pixel's bytes).

For the formats packed with integer masks and shifts (`PFF_NATIVEENDIAN`: 20 of the 42, the 32-bit
ones the blend uses among them), `src/hooks/pixel_util.cpp` does the same steps on the host: the
same `Bitwise::fixedToFixed` (with the guest's 32-bit unsigned arithmetic), the pixel read and
written big-endian by element size, luminance and alpha handled as OGRE does, and the outputs
written in the guest's order. The other formats (floats, compressed, `PF_BYTE_LA`,
`PF_SHORT_RGB(A)`) go to the guest's own function unchanged. The table is read from guest memory at
every call, as the guest does. `--native_pixel_util=false` runs the guest's functions everywhere.

Checks:

- `pixel_util_test`: against OGRE 1.7's own code (written in the test as OGRE has it, big-endian),
  every format: every value of each channel alone and a million random pixels and colours, both
  directions; `fixedToFixed` for every n, p up to 16.
- `--diag_pixel_util_check` (diagnostics builds): in the game, every integer format through the
  hooks against the recompiled originals: every value of each channel alone and 100 000 random
  pixels and colours, both directions.
- `--diag_wardrobe_composite` (diagnostics builds): a digest of every pixel each wardrobe build
  packs into the base (the pack call returning to `0x822DD384`), to compare the same equipment with
  and without the host path.

## The host blend loop

`src/hooks/wardrobe_blend_hooks.cpp` runs the whole loop above on the host, one call per item
image (`--native_wardrobe_blend`, default on; off, or with `--native_pixel_util=false`, the
recompiled loop runs). It is a ReXGlue mid-function hook (`config/torchlight_hooks.toml`): the
codegen inserts, after `rotlwi r26,r10,0` (`0x822DD21C`), `if (WardrobeBlendLoop(ctx.r1, r19,
r26, ctx.fpscr)) goto loc_822DD39C;` and changes nothing else in the generated C++.

- Only two of the build's locals go to the hook (r19, the base's format, and r26, the rows, which
  it leaves 0 as the loop does): a local passed by reference has to live in memory around every
  other call of the function. Passing the fifteen registers the loop uses made the host code of
  the whole function 8 % larger and doubled its stack accesses; with these two it is 0.4 % (39
  instructions) larger.
- The other inputs come from where the loop's registers came from (`guest_abi/wardrobe.h`
  `blend`), and the state the rest of the function can read is left as the loop leaves it: r26,
  the stack temporaries (written as the last iteration writes them), the floating-point mode.
- `tools/wardrobe_blend/check_generated.py` runs in every game build and checks the generated
  function against the one the hook was checked on (its digest, the same with the SDK CI builds)
  and what the hook relies on (the hook's place, the inputs' only definitions, what the loop
  writes, what the rest of the function reads after it). When something differs the build does not
  fail: the hook keeps the recompiled loop and the startup log says why; the development test
  `wardrobe_blend_generated` fails instead.

Measured on the desktop like the colour pack/unpack above (A B B A runs, four builds each, perf at
999 Hz, the build's samples inclusive): 156 and 148 samples without, 88 and 78 with, the build
from about 38 ms to about 21 ms, of which the host loop is about 8 ms (31 and 33 samples).

What the remaining 8 ms are, measured before trying to shorten them (the host loop alone over the
same three 256 x 256 layers, outside the game, on the same notebook):

- The scalar work each pixel needs for the bytes to stay the recompiled code's: the float/double
  conversions, the three `std::fma`, the truncation with its edge cases, the unpack and pack byte by
  byte. About 5.5 ms of it.
- The guest-to-host address translation of every byte: about 0.5 ms (5.8-6.3 ms translated against
  5.4-5.5 ms with a flat memory). Translating once per row would save at most that, some 2 % of the
  build; not done.
- `-mfma` (the hardware fused multiply-add instead of the C library's `fma`): no change.

A possible later step, not planned: four pixels at a time with SIMD (SSE/AVX on x86, NEON on arm64),
each lane doing the same IEEE operations in the same order. It could take most of the 5.5 ms. The
risk is the exactness: every vector operation has to give the scalar one's result for every input
(the double rounding of each `fma` to single, `fctidz` on NaN and out-of-range values, the mixed
opaque and blended pixels of one group, the generic formats left to the scalar loop), on each
instruction set separately. It would need the same proof as this loop: the test against the
recompiled loop written out, `--diag_wardrobe_blend_check` on the desktop and on arm64, and the
measurements.

Checks: `wardrobe_blend_test` (the host loop against the recompiled loop written out instruction by
instruction, ten formats each way, random images), and in the game `--diag_wardrobe_blend_check`
(diagnostics builds): each image's loop computed on the host into a copy, then the recompiled loop
run, every pack it makes compared with the host's pixel and the stack temporaries after the last.

## PC

Torchlight PC (`Torchlight.exe`, x86, with OGRE 1.6.1 in `OgreMain.dll` on Direct3D 9) builds the
equipment texture with the same function and the same blend. What follows is read from the
binaries (no source exists) and estimated; nothing of it is measured on PC.

Where it is:

- The function at `0x4E13D0` (to about `0x4E3E34`, 10.8 KB) is the only caller of
  `PixelUtil::unpackColour` (import slot `0xA5C8E4`, at `0x4E2200` and `0x4E225A`) and
  `PixelUtil::packColour` (slot `0xA5C8E8`, at `0x4E2230` and `0x4E2372`) in the executable. It
  also calls `Image::getPixelBox` (`0x4E1BB0`, `0x4E219A`) and `Image::load` (`0x4E14CA`,
  `0x4E2133`), and it reads the unit data's `WARDROBE` (`0x4E1D70`), `CLASS` (`0x4E1DF5`),
  `TEXTURE` (`0x4E1E99`) and `MESH` (`0x4E1EE7`) keys (UTF-16 strings at `0xA70888`, `0xA71554`,
  `0xA6EF80`, `0xA70DD0`). `rimlight.dds` is not used by it: it comes from the materials.
- In `OgreMain.dll` (base `0x10000000`) the two are `unpackColour` at RVA `0x11C9E0` and the 8-bit
  `packColour` at RVA `0x11C8A0`.

The blend, `0x4E21E0` .. `0x4E2384`: the same loop as above (rows by columns, both images
stepped 4 bytes; unpack the item's pixel; alpha of 255 or more packs it as it is; else unpack the
base's, alpha = a x 1/255 (the float at `0xA68E6C`), 1 - alpha, each channel item x alpha + base x
(1 - alpha), truncated (`fistp` with the rounding control set to chop, its low byte), packed with
alpha 255). The arithmetic is x87: no fused multiply-add, and the precision is the FPU's precision
control (single under Direct3D 9 without `D3DCREATE_FPU_PRESERVE`, OGRE's default), so a channel
can differ by one from the console's; that matters only for comparing the two versions, not on PC.

What the console port's numbers say for PC:

- The recompiled loop and its calls were about 18 ms of a 37 ms build (with the host
  pack/unpack, desktop notebook). The same loop compiled for x86-64, with the pack and unpack out
  of line as OGRE's DLL calls are and the x87 truncation the game does, takes about 4.1 ms for the
  same three layers of 65 536 pixels on the same i7-8750H (21 ns a pixel, warm caches). About
  14 ms of the 18 were the recompilation's: every byte through the guest-to-host address
  translation, big-endian loads and stores, the calls through the guest's ABI and the stack
  temporaries in guest memory.
- The rest of the build (meshes, materials, entity, the image decode, allocations) was about 13 ms
  recompiled; natively it should be a few times less, a few milliseconds.
- So on PC the blend is likely about half of the equip's work, and the whole build well under a
  60 fps frame (some 6 to 10 ms on this CPU). An equip hitch on PC, if there is one, would come as
  much from the rest (the file loads, the Direct3D 9 texture upload) as from the blend.
- C1 has nothing to replace on PC (OGRE's pack and unpack are native there); a C2-style
  replacement of the loop (a detour at `0x4E21E0` to the exit) could save most of the ~4 ms.

To confirm on PC (under Proton/Wine on Linux `perf` samples the game's threads; `Torchlight.exe`
loads at its base `0x400000`, `OgreMain.dll` at `0x10000000`):

1. The equip frame's time against the frames around it, picking up armour as in the console runs
   (one piece, then several).
2. Samples inside `0x4E13D0` .. `0x4E3E34` and its callees, inclusive: the whole build.
3. Of those, the loop `0x4E21E0` .. `0x4E2384` plus `unpackColour`/`packColour` in `OgreMain.dll`:
   the blend's share.
4. The rest: `Image::load`, the mesh and entity creation, the texture's update, and any frame
   spent waiting on files.

## For a port to another build

On a build of the game with the same functions (OGRE 1.7 and this wardrobe code), the same two
replacements give the same saving, and the whole loop can be replaced as here
(`hooks/wardrobe_blend.h` has no guest or SDK dependency: it takes its memory as an argument),
which also saves the loop's own share; on a build without a recompiler's mid-function hook, by
replacing the loop's code where it starts and returning to where it ends. The checks above are the
way to prove a port: identical bytes against the original functions for every input, and identical
base pixels for the same equipment.
