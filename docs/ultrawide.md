# Other aspect ratios (TLR-006): research

Goal: have the game really draw in 21:9, 32:9 and 16:10 (Steam Deck), not 16:9 with bars. Research
only (reading the guest code and the SDK); nothing implemented. To test, gamescope with the target
aspect ratio.

## How it is today

- The guest always draws a 1280×720 frame: its render window (D3D9RenderWindow) takes the size of
  the console's **video mode**, not the `RES_WIDTH`/`RES_HEIGHT` from the settings (verified in phase
  2 of settings: it asks for 800×600 and draws 1280×720).
- The backend receives that frame in guest coordinates and presents it letterboxed in the window
  (`tl_backend_present`); its guest size is fixed in `LiveOptions` (1280×720).

## What already exists and helps

1. **The SDK's video mode is configurable.** `VdQueryVideoMode` reads the `video_mode_width` /
   `video_mode_height` cvars (640…4095; `xboxkrnl_video.cpp:42-74`) and sets `is_widescreen` from the
   aspect ratio. It is the door to make the guest believe the screen is 1680×720 (21:9), 2560×720
   (32:9) or 1280×800 (16:10).
2. **The game derives the camera from the window size.** `sub_8220A470` ("CreateViewports") and the
   aspect change handler `sub_8220A860` (in a listener vtable; it logs "AspectRatio message - W x H"
   and "stored res is") call `sub_82204378(camera, width, height)`, which does
   `Frustum::setAspectRatio(width / height)` (the camera's virtual 71), another virtual with the
   dimensions (121) and `sub_82223080`. The SceneManager's culling uses that frustum, so a wider
   window gives a wider view **with no pop-in at the edges**.
3. **The UI already has an aspect mechanism** (360 games had to support 4:3):
   - layout scaling (`kGameUiScaleLayout`, `sub_823387F0`) multiplies the areas by the settings'
     `X_RATIO` / `Y_RATIO` factors (indices `0x83558F54` / `0x83558F58`);
   - the 360 layouts use absolute sizes with anchors (`HorizontalAlignment` / `VerticalAlignment`,
     "HORIZONTAL:CENTER" in Runic's format): with a wider root, what is anchored to the right goes to
     the right edge and what is centered stays centered, without stretching.

## What to do or verify, by area

| Area | What happens with another aspect ratio | What to do |
|---|---|---|
| Guest video mode | Fixed 1280×720 today | Set `video_mode_width/height` to the window's aspect ratio (height 720, width from the ratio) before the guest starts. Verify that the guest's D3D accepts non-standard widths (in only mode there is no real EDRAM, but there are buffer reservations and tiling computations) |
| Backend | Guest size fixed at 1280×720 in `LiveOptions`; 16:9 letterbox | Take the guest frame size from the video mode (or from the main render target the guest creates) instead of the constant. The internal scale already works on any guest size |
| Camera and culling | Follows the window (point 2) | Verify in the run that the view widens and that no objects show up late at the edges |
| UI (HUD, menus) | Absolute anchors | Verify the values of `X_RATIO`/`Y_RATIO` (default, and whether the game recomputes them when the aspect changes); check that the HUD stays in the corners and the menus centered. Possible problem: layouts with absolute positions meant for a width of 1280 without an anchor |
| Minimap | It is a fixed viewport (in the captures, 187×187 at 1045,466) | See where the game takes that position from: if it comes from the HUD window (anchored), it moves by itself; if it is a constant, fix it in `CreateViewports` or in the viewport hook |
| Fullscreen images (loading, cinematics, vignettes) | They are 16:9 textures in `{0,0,1,1}` windows: they would stretch | Pillarbox: limit those windows to a centered 16:9 (identify them by layout structure, not by texture name) |
| Shadow maps and lights (RT 207/208) | They cover the area around the player according to their own projector | Verify that they cover the wider view; otherwise there would be edges without shadows or lights |
| Mouse/pointer | Not applicable (pad) | – |
| 16:10 (Deck, 1280×800) | Taller view | Same path with height 800; check the HUD anchored at the bottom |
| 32:9 | Width of 2560 at a height of 720; the SDK caps at 4095 | The guest at a height of 720 and the internal scale provides the pixels (5120×1440 = 2560×720 × 2) |

## Risks

- The guest's D3D could assume standard mode widths (1280, 1920) in some computation; with the null
  GPU there is no EDRAM to overflow, but there could be asserts or buffer sizes.
- Runic's UI could have absolute positions without an anchor in some menu (they would look shifted).
- Content the game never showed outside 16:9 (map edges, scenery) could look incomplete at 32:9.

## Follow-up tickets

- **TLR-006.1 Experiment without code:** a run with `--video_mode_width=1680 --video_mode_height=720`
  (and gamescope at 21:9) and F9: does the guest create the 1680×720 window, does the camera widen,
  what happens with the HUD? The backend will still letterbox to 16:9 (the image will look
  squeezed): the capture serves to see the guest's real frame. Criterion: the log line
  "AspectRatio message - 1680 x 720" and a 1680×720 frame in the capture.
- **TLR-006.2 Backend with the guest frame size:** the size comes from the guest, not from a
  constant; replay of a 21:9 capture from TLR-006.1 without letterbox. Test: replay.
- **TLR-006.3 Automatic aspect ratio:** the window's aspect ratio (or a setting in the Video column)
  sets the video mode before the guest starts (requires a restart).
- **TLR-006.4 UI:** check the HUD, menus and minimap at 21:9 and 16:10; fix whatever does not follow
  the anchors.
- **TLR-006.5 16:9 fullscreen:** pillarbox of loading images and cinematics.
- **TLR-006.6 32:9 and shadows/lights:** verify the coverage of RTs 207/208 and the map edges.
