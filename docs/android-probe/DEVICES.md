# Android devices: read-out (read-only, over adb)

## POCO X7 5G (the main device), 2026-10-17

| | |
|---|---|
| Model | POCO X7 (24095PCADG, `malachite`) |
| Android | 16, API 36 |
| SoC | MediaTek MT6878 (Dimensity 7300 class): 4× Cortex-A78 (0xd41) at 2.5 GHz (cpu4–7), 4× Cortex-A55 (0xd05) at 2.0 GHz (cpu0–3) |
| RAM | 11.2 GiB (`MemTotal` 11707164 kB) |
| ABI | arm64-v8a only |
| Page size | 4096 (`getconf PAGESIZE`) |
| GPU | ARM Mali-G615 MC2 |
| GLES | OpenGL ES 3.2 v1.r44p1 |
| Vulkan | device 1.3.247 (`feature:android.hardware.vulkan.version` 1.3.0, level 1; loader 1.4.0) |

Texture compression:

- **DXT/S3TC: none.** There is no `GL_EXT_texture_compression_s3tc` or any other S3TC, DXT, RGTC
  or BPTC variant in the GLES extensions, and Vulkan reports `textureCompressionBC` 0.
- **ETC2:** yes. It is core in GLES 3.0+, and Vulkan reports `textureCompressionETC2` 1. ETC1 also
  (`GL_OES_compressed_ETC1_RGB8_texture`).
- **ASTC:** yes, LDR and HDR (`GL_KHR_texture_compression_astc_ldr/hdr`, `_sliced_3d`,
  `GL_OES_texture_compression_astc`); Vulkan reports `textureCompressionASTC_LDR`/`_HDR` 1.

## Galaxy Tab S5e (the floor)

Pending: USB debugging.
