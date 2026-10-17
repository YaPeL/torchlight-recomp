# Porting Torchlight's rendering to OGRE 14: notes

What had to be solved for Torchlight to look right on OGRE 14.6, written for someone porting the
PC game (OGRE 1.6.5, Direct3D 9) to OGRE 14 from source, without this project's hooks. Each item
gives the symptom we saw, the cause and what fixed it, with a pointer to the code, a document or a
commit of this repository.

## How to read this

This project does not port the game's source. It runs the Xbox 360 build (OGRE 1.7, Runic's
shaders) and rebuilds every draw it submits on a host OGRE 14 (`src/backend/backend.cpp`,
`docs/ARCHITECTURE.md`). So the reference for "right" was the console's image, and the backend
drives OGRE's `RenderSystem` directly, with no scene manager of its own. Three kinds of items
follow, marked as such:

- **[any port]**: OGRE 14 behaves this way for everyone; a source port meets it.
- **[direct]**: only when driving the `RenderSystem` without OGRE's `SceneManager`, as we do. A
  source port that renders through `SceneManager` gets these for free; they are here because the
  cause explains what OGRE does for you.
- **[Xbox]**: specific to the console build. Noted where a PC port should compare against the PC
  game instead of copying our answer.

On the references: the move to OGRE 14 and to the RTSS was done before this repository's public
history starts (`bcc0179`), so those items point to the code and to `docs/ARCHITECTURE.md`; later
fixes point to their commits.

## 1. Fixed-function materials through the RTSS

OGRE 14's GL3+, Direct3D 11 and Vulkan render systems have no fixed function. Every material
pass that does not name its own programs needs the Run Time Shader System (RTSS) to generate
them. Torchlight PC's materials are mostly fixed-function passes (texture stages, vertex colour,
one directional light, linear fog, alpha test), so almost every draw goes through it.

### 1.1 One program per structure, and what goes into the key [any port]

- **Symptom:** hitches of 17-115 ms the first time something is drawn, in the middle of play,
  on the first run after a driver update.
- **Cause:** the RTSS generates and compiles a program pair per distinct pass structure. Anything
  that changes the generated code makes a new pair: whether there is lighting, fog, vertex colour,
  each stage's operation and sources, and the constant colours of `LBS_MANUAL` sources, which the
  RTSS compiles into the fragment program.
- **Solution:** keep the structure stable and let everything else be a constant. Our loading
  screens had no light or fog and produced a second variant of pairs already compiled during the
  load (60 compilations in play). Keeping light and fog in the program with neutral values (diffuse
  0, fog factor saturated to 1) brought one recorded session from 187 to 111 programs, about one
  per pair. The alpha test function became a uniform instead of compiled code (1.4). What remains
  is created during loads. The driver's own shader cache covers later runs: median 1.3 ms and at
  most 5 ms warm, against median 17 ms and up to 115 ms cold. See `docs/ARCHITECTURE.md`,
  "Shader compilation", and the program key in `backend.cpp` (`ProgramKey`, `MakeProgramKey`).

### 1.2 The RTSS's lighting saturates the colour [any port; compare with PC]

- **Symptom:** lit textures came out too dark or flat where the console's were bright; draws
  with `MODULATE_X2` or `X4` lost their range.
- **Cause:** the RTSS's `FFPLighting` clamps the lit colour (`FFP_SATURATE`) before the texture
  stages. The Xbox build's own fixed-function emulation (Runic's `FFP_Light_Directional_Diffuse`)
  does not clamp, and its stages multiply the unclamped colour.
- **Solution:** a SubRenderState of our own, `GuestLighting`, replaces `FFPLighting` with the
  console's formula (`backend.cpp`, "RTSS replacement of FFPLighting"). Before that, a workaround
  divided the light and compensated in the stage. It was equivalent only while a single stage
  consumed the colour, and it went away with the replacement.
- **For a PC port:** Direct3D 9's fixed function clamps per-vertex lit colours to [0, 1] too, so
  on PC the RTSS's clamp may be what the game was made for. Compare against the PC game before
  replacing anything.

### 1.3 A custom lighting stage must pass the vertex colour itself [any port]

- **Symptom:** with our own lighting stage, draws that track the vertex colour came out as if the
  vertex colour were white (an apparent factor of 2 in a first attempt).
- **Cause:** the RTSS links the vertex colour to the fragment program only for its own
  `FFPLighting` (`fixupFFPLighting`). A replacement is not covered.
- **Solution:** the replacement declares the vertex colour input and the interpolated output
  itself (`GuestLighting` in `backend.cpp`).

### 1.4 Alpha test [any port, the fix direct]

- **Symptom (direct):** alpha-tested foliage and decals drawn as solid quads, or with the wrong
  function.
- **Cause:** GL3+, Direct3D 11 and Vulkan have no fixed-function alpha test; it has to be part of
  the fragment program. The RTSS's `FFPAlphaTest` reads the function from a per-renderable update
  that `SceneManager` runs and a direct `RenderSystem` user does not.
- **Solution:** through `SceneManager`, the pass's `setAlphaRejectSettings` is enough. We added
  our own stage, `GuestAlphaTest`, with the compare function as a uniform. One program then serves
  every function, the RTSS appends an index to the uniform's name, and it runs last, after fog
  (`backend.cpp`, "RTSS alpha test").

### 1.5 The RTSS's fog also fogs the alpha [any port]

- **Symptom:** alpha-blended draws in fog (the water's waves in a mine level) came out brighter
  than the console's. Computed by hand on four pixels, the RTSS's output matched exactly with the
  alpha blended towards the fog's.
- **Cause:** the RTSS's fog, per vertex and per pixel, lerps the alpha towards the fog colour's
  alpha as well as the RGB. Direct3D 9's fixed-function fog and the Xbox build's emulation
  (`FFP_PixelFog_Linear`) change only the colour.
- **Solution:** a replacement fog stage, `GuestPixelFog`, computes the linear factor per pixel and
  blends only RGB (`backend.cpp`, "RTSS replacement of FFPFog"). It took the waves capture from
  36.87 to 37.15 dB against the console. This applies to the PC game too: its D3D9 fog never
  touched alpha.

### 1.6 Texture coordinate generation [any port; the details Xbox]

- **Sphere map:**
  - **Symptom:** environment-mapped items (sphere maps) shimmered and mapped wrong.
  - **Cause:** the Xbox build computes them from the view-space normal,
    `(n.x / 2 + 0.5, -n.y / 2 + 0.5)`. OGRE's `TEXCALC_ENVIRONMENT_MAP` follows GL's sphere map,
    which uses the reflected eye vector.
  - **Solution:** normal-based texgen with a matrix (`SphereMapMatrixForHost` in
    `xbox_to_gl_conventions.h`).
- **View normal on 2D textures:**
  - **Symptom:** missing coordinates.
  - **Cause:** the RTSS's normal-map texgen only works with cube maps.
  - **Solution:** our `GuestTexgen` stage computes it for 2D textures.
- **Reflection:**
  - **Symptom:** reflections in the wrong place. Four draws were skipped until this was solved.
  - **Cause:** the RTSS normalizes the normal and reflects in its own space. The console's reflects
    with the unnormalized normal and takes the result back to world space with z negated.
  - **Solution:** `GuestTexgen` plus `ReflectionMatrixForHost`.
- **For a PC port:** these are the console's formulas. PC's reference is D3D9's
  `D3DTSS_TCI_CAMERASPACENORMAL` and `CAMERASPACEREFLECTIONVECTOR`; check each texgen material
  against the PC game, and expect the RTSS's cube-map-only normal texgen to need the same kind of
  stage.

### 1.7 Hardware skinning [any port]

- **Symptom:** none visible today. It is a latent case found by reading the code.
- **Cause:** the RTSS's `HardwareSkinning` skins the normal only when the pass has lighting. A
  skinned, unlit pass that uses the normal for texgen would use the unskinned normal. Its linear
  skinning also blends influence k with index lane k and weight lane k, with the weights taken as
  summing to 1.
- **Solution:**
  - Check that every skinned pass with texgen is also lit. We checked 13 skinned vertex programs:
    `docs/ARCHITECTURE.md`, "Skinning without lighting".
  - Normalize the weights in the mesh data. `BlendLanesForHost` does it for the console's lane
    order.
  - Keep the bone palette within the vertex program's constant budget; a palette that does not
    fit is skipped with its reason.

### 1.8 Blend modes and the uninitialized arguments of `LayerBlendModeEx` [any port]

- **Symptom:** state caches that never hit, and two recordings of the same scene that never
  compared equal. In our case, 12 % of the texture-blend records in still scenes were spurious
  changes.
- **Cause:** `TextureUnitState`'s constructor sets only the alpha mode's type, operation and
  sources; `alphaArg1`, `alphaArg2` and `factor` stay uninitialized until `setAlphaOperation`
  runs. Neither `colourArg1` nor `colourArg2` of an alpha-type mode is ever set. This is the same
  in OGRE 1.6/1.7 and 14.6 (`OgreTextureUnitState.cpp`), and `LayerBlendModeEx::operator==`
  compares `factor` for every operation.
- **Solution:** never key a cache, a hash or a comparison on the whole struct. Take the colour
  arguments only when a source is `LBS_MANUAL` in a colour mode, the alpha arguments likewise in an
  alpha mode, and the factor only for `LBX_BLEND_MANUAL`. That is exactly what the RTSS's
  `FFPTexturing` reads, so the generated code is unaffected; only your own code is. Commits
  `cf8721f` (the record and its test, `hooks/texture_blend_record.h`) and `b3b1e21` (why the
  program key leaves the factor out).

### 1.9 A light's direction depends on its node's history [direct]

- **Symptom:** the same draw got programs whose light direction differed in the last digit,
  depending on what was drawn before.
- **Cause:** `setDirection` on a light attached to a node is relative to the node's current
  orientation.
- **Solution:** set the orientation absolutely, and only when it changes (`0a4f50a`).

## 2. Shaders of your own

- **One source for every render system** [any port]: the backend's own programs (gamma, the
  present copy, the host UI) are written once with OGRE's unified shader macros
  (`OgreUnifiedShader.h`, in OGRE's Main media) and compiled as GLSL or HLSL. In HLSL a sampler
  bound by register has no named constant, so set sampler names only where they exist (`backend.cpp`,
  "full-screen passes").
- **Custom SubRenderStates carry over** [any port]: stages built through the RTSS API
  (`callFunction`, `callBuiltin`, FFPLib dependencies) instead of GLSL text generated HLSL on
  Direct3D 11 with no change (`docs/windows-port.md`, "WIN.7 done").
- **Shader cache** [any port]: we rely on the driver's cache (NVIDIA, Mesa including the Steam
  Deck, Windows). OGRE's microcode cache has a bug in 14.6. When the driver rejects a binary from
  the cache, GL3+ recompiles and replaces the entry without marking the cache modified
  (`GpuProgramManager::addMicrocodeToCache`), so the rejected binary stays in the file and that
  program is recompiled silently on every run (`docs/ARCHITECTURE.md`, "OGRE 14.6 bug").

## 3. Textures and formats

- **Small mip levels of compressed textures:**
  - Kind: [any port, D3D11].
  - **Symptom:** on Direct3D 11, smeared or wrong colours on distant DXT surfaces.
  - **Cause:** OGRE's Direct3D 11 uploads DXT levels smaller than a 4x4 block wrong and drops the
    last two anyway (`D3D11Texture::_create2DTex`).
  - **Solution:** keep only the levels of at least 4x4 on every render system
    (`CompressedMipmapsForHost`). It also brought GL3+ closer to the console (+0.08 and +0.16 dB on
    two captures).
- **Uncompressed images get generated mips** [any port]: load them with `MIP_DEFAULT` and OGRE
  builds the chain. The game asks for generated mips on uncompressed textures
  (`tl_backend_texture_file`).
- **Codecs** [any port]: DDS is built into OgreMain. PNG needs a codec plugin; we load
  `Codec_STBI` (`backend.cpp`). The PC game loads its images through FreeImage, so check each image
  type its data uses against the codecs you build.
- **Vertex colours:**
  - Kind: [any port].
  - **Symptom:** OGRE 14 logs "VET_COLOUR_ARGB element type is deprecated and incurs conversion on
    load" for meshes made for D3D9.
  - **Cause:** OGRE 14 wants byte order R, G, B, A (`VET_UBYTE4_NORM`) on every render system.
  - **Solution:** convert the meshes once with OGRE's mesh upgrader instead of on every load. We
    convert D3DCOLOR bytes ourselves (`VertexColourD3DToGl`).
- **Reading a render target mid-frame:**
  - Kind: [any port, GL3+].
  - **Symptom:** after a `convertToImage` or similar read of a render target in the middle of a
    frame, the following draws came out wrong. It made the fog look broken during one
    investigation.
  - **Cause:** the read leaves GL texture and framebuffer bindings different from what OGRE
    believes it has.
  - **Solution:** read back only after the frame (`docs/ARCHITECTURE.md`, "Reading a render target
    mid-frame").

## 4. Coordinates

- **Half pixel:**
  - Kind: [any port].
  - **Symptom:** on GL, the whole UI was half a pixel up and to the left, the last row and
    column of full-screen quads stayed uncovered, and there were black lines around some icons.
  - **Cause:** Direct3D 9 samples pixel (i, j) at (i, j); every other render system samples at
    (i + 0.5, j + 0.5). Code written for D3D9 offsets its quads by -0.5. In OGRE 14 only the
    Direct3D 9 render system returns a non-zero `getHorizontalTexelOffset`.
  - **Solution:** remove hard-coded D3D9 offsets, or take them from
    `getHorizontalTexelOffset`/`getVerticalTexelOffset`. We move the clip matrix by half a pixel
    (`D3DPixelCentresToGl`); PSNR against the console went from 24-28 to 31-70 dB on the menus and
    loading screens.
- **Depth range and render-texture origin** [any port, mostly free]: OGRE converts projections per
  render system (`_convertProjectionMatrix`: [-1, 1] on GL, [0, 1] on D3D11) and flips clip y for
  render textures that need it (`RenderTarget::requiresTextureFlipping`; GL stores them bottom up).
  Through `SceneManager` both are automatic. Driving the render system directly, the projection you
  hand to the auto parameters must go through the same path (`backend.cpp`, `ApplyProgram`), and a
  hand-made full-screen copy between render textures must keep memory order on each render system
  (`RenderTextureCopyQuad`).

## 5. Differences between render systems

**GL3+ (Linux, Windows, macOS):**
- **OpenGL 3.3 core is required.** Without it, OGRE's GL3+ crashes while creating its window on
  Windows, before its own version check. We probe for 3.3 first and fall back to Direct3D 11
  (`docs/windows-port.md`, "Without OpenGL 3.3"). An exception thrown from the plugin must be
  handled before the plugin is unloaded (`729c479`, `5a2ac48`).
- **Clears** take the viewport's area (OGRE scissors to it).
- **NVIDIA's driver spins in its vsync wait:** 168 % of a core at the menu against 89 % on
  Direct3D 11 on the same machine, with the same frame times.
- **macOS:** Apple's GL is 4.1 on Metal. OGRE takes an `NSView` as `externalWindowHandle`, and
  AppKit wants the context attached on the main thread (`10ce0d3`, `docs/macos-port.md` section 4).
- **Small differences between drivers:**
  - NVIDIA's polygon offset unit is effectively twice Direct3D 11's: the edges of lanterns, 0.3 dB
    on one capture.
  - Mesa and NVIDIA differ by up to 5 levels per channel on 2D screens.
  - Compare against a reference made on the same driver family (`docs/ARCHITECTURE.md`, "Mesa
    against NVIDIA").

**Direct3D 11 (Windows; the default there):**
- **Clears:**
  - **Symptom:** a clear of part of a target wiped all of it.
  - **Cause:** `clearFrameBuffer` clears the whole target whatever the viewport.
  - **Solution:** we measure which behaviour the render system has at startup and draw a quad for
    partial clears (`ClearViewport`, `MeasureClearArea`).
- **Depth bias:**
  - **Symptom:** decals and shadows z-fighting differently from GL.
  - **Cause:** OGRE's Direct3D 11 multiplies the constant depth bias by 10 before using it.
  - **Solution:** divide by the render system's factor (`DepthBiasConstantForHost`).
- **DXT mip levels:** see section 3.
- **One constant buffer per stage, not per program:** a cache of "constants already bound" has to
  be per stage and forgotten when the program changes (`daeaa8b`).
- **Result:** the 20 reference captures within 0.1 dB of GL3+ (one at 0.3 dB, the polygon offset
  above), with half the CPU of GL3+ on NVIDIA.

**Metal: not viable today.** OGRE 14.6's Metal render system is experimental, and the RTSS has no
Metal program writer (`ProgramWriterManager` registers `glsl`, `hlsl`, `glslang` and `glsles`).
Every RTSS-generated material needs one (`docs/macos-port.md`, section 4).

**Vulkan: tried in the replay, not working without changes.** The RTSS programs compile through
the glslang plugin, but the render system expects a frame laid out its way:
- a buffer upload inside a render pass is an assertion;
- `clearFrameBuffer` only sets the next pass's load colour, so clears of parts of a target in the
  middle of a frame do nothing;
- its window has to be made outside it.

With the pass ended before each upload a capture replayed to the end with every draw issued, and
the image was black. A port that uploads dynamic buffers between draws (Torchlight's particles and
UI do) needs either changes to that render system or a frame recorded with uploads first and clears
as load actions (`docs/performance-profile.md`, "The backend thread").

## 6. Performance changes that mattered

Measured on a laptop with a GTX 1050 Ti and an Intel UHD 630 (`docs/performance-profile.md`). Most
of these are what OGRE's `SceneManager` already does. They are listed because a port that issues
draws through the render system per object, or that patches OGRE's flow, can lose them.

- **Programs cached by structure, never per draw** (1.1).
- **One vertex declaration per layout, reused:**
  - Kind: [any port].
  - **Cost before:** a `VertexData` per draw meant a declaration, and in GL3+ a vertex array
    object, created and destroyed for each draw.
  - **Trap:** GL3+ re-specifies a VAO only when the bound buffers' addresses change. A freed
    buffer whose address a new one reuses goes unnoticed, so remember what was bound
    (`backend.cpp`, "Vertex data per declaration layout").
- **GPU buffers kept per resource, rewritten with discard,** instead of one buffer created and
  destroyed per version of a dynamic buffer [any port]. On the Intel GPU, together with the VAO
  change and a quadratic loop of ours, a recorded session went from 88-90 s to 78-80 s.
- **Constants bound only for the classes that changed** (`daeaa8b`) [direct]: the render system
  gets only the `GpuParamVariability` classes whose values changed. `SceneManager` itself binds
  only `GPV_PER_OBJECT` between objects of one pass.
- **The camera's projection set only when it changes** (`0a4f50a`) [direct]: setting a custom
  projection marks the frustum out of date, and the data source rebuilds it and its inverse for
  every draw.
- **Viewports left alone when unchanged** (`db5c15a`) [direct]: `Viewport::setDimensions` marks
  the viewport updated even with the same values, and GL3+ then rebinds the target and its
  rectangle.
- **Samplers cached per state** [any port]: OGRE 14 has `Sampler` objects. Create one per
  filtering/addressing/anisotropy combination, not per texture unit change.
- **Vertex bindings touched only where they change** (`6a84de1`).
- **Hybrid-GPU laptops** [any port]: on NVIDIA with PRIME, `eglSwapBuffers` blocks 2.6-3.3 ms for
  the copy to the integrated GPU's display, which caps presentation near 180 fps whatever the
  game does. Not a rendering cost (`docs/performance-profile.md`, "The presented ceiling on
  hybrid-GPU laptops").

## 7. Known traps, in short

- Do not compare or hash a whole `LayerBlendModeEx` (1.8).
- The RTSS's lighting clamps and its fog fogs alpha; decide against the PC game (1.2, 1.5).
- A custom lighting stage must pass the vertex colour (1.3).
- Alpha test needs the RTSS or a stage of your own; no render system but D3D9 has it in hardware
  (1.4).
- Remove D3D9 half-pixel offsets (4).
- Keep DXT levels of at least 4x4 (3).
- Do not read render targets back mid-frame on GL3+ (3).
- OGRE's microcode cache can keep a rejected binary forever (2).
- Direct3D 11: whole-target clears and the x10 depth bias (5).
- Vulkan and Metal are not drop-in (5).

## What this does not cover

A source port also meets things this project never touched, because the console build had
already dealt with them or because they sit above the render system: the PC material scripts'
and compositors' syntax changes between 1.6 and 14, the CEGUI renderer for OGRE 14, the mesh and
skeleton file versions, and the PC build's own shaders, if it has any outside fixed function.
None of that is verified here.
