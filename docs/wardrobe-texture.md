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
  multiply-adds (`fmadds`), so a port must use `fmaf` (or the same fused operation) to give the
  same bytes.
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

## For a port to another build

On a build of the game with the same functions (OGRE 1.7 and this wardrobe code), the same two
replacements give the same saving; the blend loop above can also be written natively in full
(reading A and B, blending, writing B) with the same single-precision fused arithmetic, which would
also save the loop's own share. The checks above are the way to prove a port: identical bytes
against the original functions for every input, and identical base pixels for the same equipment.
