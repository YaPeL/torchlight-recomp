# Text input in only mode (TLR-010)

Problem: with `--native_live=only` the keyboard to type the character's name does not appear and a
new character cannot be created. With Xenos a host dialog appears.

Static research (recompiled code in `~/torchlight-rewrite/generated/default`, `default.xex` image
decrypted with `tools/xex/xex_dump.py`, SDK in `~/rexglue-sdk` commit `0c7b01a` with the patches in
`patches/`). The game was not run.

## Summary

- The guest uses `XamShowKeyboardUI` in a single place: the new game menu (`CNewGameMenu`), for the
  player's name and the pet's name. It calls it asynchronously (with an XOVERLAPPED) and polls the
  result every frame; it does not block.
- The SDK serves it with an ImGui dialog (`KeyboardInputDialog`) drawn on the runtime's
  `ImGuiDrawer`. `ReXApp` only creates that drawer if the GPU plugin has a presenter. The `null`
  plugin of only mode has none, so the drawer is null and the SDK falls back to its headless path:
  it completes the operation at once, successfully, copying the default text into the buffer. The
  game gets "accepted" with the name it already had (empty for a new character) and nothing is ever
  shown.
- **The same problem affects `XamShowMessageBoxUI`**: in only mode every message box is answered by
  itself with the active button. The guest has 9 callers, among them "Existing Save … Do you want to
  overwrite the save?" and "Corrupt/Damaged Save … can not proceed unless the corrupt/damaged save
  game is deleted". In both the active button is 0 = "No", the non-destructive one: nothing is
  overwritten or deleted. The risk is a different one: the game may go on without saving without the
  user knowing (medium risk, see "Other flows").
- **Proposed design, without patching the SDK**: give the runtime a "detached" `ImGuiDrawer` (no
  presenter) whose geometry the OGRE backend draws. The SDK already supports that mode and exposes
  everything needed as public API. With it the keyboard and the message boxes work again exactly as
  with Xenos. The Steam on-screen keyboard comes for free: the focus of an ImGui `InputText` turns on
  `SDL_StartTextInput`, and SDL opens the Steam keyboard when Steam asks for it.

## 1. What the guest does

### Call

`CNewGameMenu` (RTTI `.?AVCNewGameMenu@@`, vtable `0x820D2658`):

- Slot 9, `sub_8238E7F0`: handles the menu control events. For controls 62 and 63 it calls
  `sub_8238EFA0`.
- `sub_8238EFA0` (`torchlight_recomp.140.cpp`): if `this+372` is 0 (no keyboard pending), stores the
  control in `this+376`, sets `this+372 = 1`, zeroes the XOVERLAPPED at `this+344` (7 words) and calls
  `sub_8287D8B0`, the thunk of `__imp__XamShowKeyboardUI`:

| Register | Argument | Value |
|---|---|---|
| r3 | user_index | global of the active user |
| r4 | flags | 0 |
| r5 | default_text | `this+380` (current name, copied from `this+464` or `this+492`) |
| r6 | title | `this+408` |
| r7 | description | `this+436` |
| r8 | result buffer | `this+312` (UTF-16BE) |
| r9 | buffer_length | 13 characters |
| r10 | overlapped | `this+344` |

  The strings go through localization (`sub_82328148`). The keys in the image (UTF-16BE):
  `0x820D25B4` "Player Name", `0x820D25CC` "Enter a name for your player!", `0x820D2608` "Pet
  Name", `0x820D261C` "Enter a name for your pet!". Control 62 = player, 63 = pet.

### Wait and result

- Slot 3 of the same vtable, `sub_8238E068` (`torchlight_recomp.206.cpp`, from `loc_8238E3C8`),
  runs every frame: if `this+372 != 0` and the XOVERLAPPED's `InternalLow` (`this+344`) is no longer
  997 (`ERROR_IO_PENDING`), it sets `this+372 = 0` and calls `sub_8287D880`.
- `sub_8287D880` is an inline `XGetOverlappedExtendedError`: it returns 996 if the pointer is null,
  nothing if still pending, and otherwise `dwExtendedError` (XOVERLAPPED `+24`).
- If the extended error is 0: it reads `this+312`, fits it to 13 characters (`sub_8220FD18` /
  `sub_823513D8`, not analyzed in detail), sets `this+340 = 1` and copies it to the player's name
  (`this+464`, control 62) or the pet's (`this+492`). With any other value (cancel = 1223,
  `ERROR_CANCELLED`) nothing changes.

Semantics any implementation has to respect:

- Return `ERROR_IO_PENDING` and complete the XOVERLAPPED later. The guest keeps drawing the menu while
  it waits (in only mode the backend keeps receiving frames).
- On completion: the buffer holds the zero-terminated text of at most `buffer_length - 1`
  characters (12; that is how `rex::string::copy_and_swap_truncating` truncates,
  `include/rex/string.h:96`), `InternalLow = 0`, `dwExtendedError = 0` to accept or 1223 to cancel.
- Meanwhile the SDK broadcasts `XN_SYS_UI` (0x9) = true before and = false 100 ms after completing.

### Other uses

`XamShowKeyboardUI` has a single caller (`sub_8238EFA0`). The name is only asked for when creating
the character.

## 2. How the SDK serves it today

`~/rexglue-sdk/src/kernel/xam/xam_ui.cpp`:

- `XamShowKeyboardUI_entry` (line 421): if `imgui_drawer` exists, it dispatches a
  `KeyboardInputDialog` (ImGui: `InputText`, OK, Cancel) with `xeXamDispatchDialogEx`. That dispatch
  completes the XOVERLAPPED in a deferred way (`CompleteOverlappedDeferredEx`); the worker thread waits
  on a fence until the dialog closes on the UI thread. Cancel → `extended_error = X_ERROR_CANCELLED`;
  OK → converts to UTF-16, copies with truncation and `extended_error = 0`. It is exactly the
  semantics the guest expects.
- Without `imgui_drawer` (or with the `headless` cvar), `xeXamDispatchHeadless`: copies
  `default_text` into the buffer and completes successfully without showing anything.
- `imgui_drawer` comes from `Runtime::imgui_drawer()`, which `ReXApp` publishes in
  `src/ui/rex_app.cpp:233`. `ReXApp::SetupPresentation` (lines 358-381) creates the drawer in two
  cases: if the GPU plugin has a presenter (Xenos: overlays painted by the presenter) or if there is
  no GPU plugin (`!graphics_system`, "detached" mode: the app provides its `ImmediateDrawer` with
  `OnCreateImmediateDrawer`). The `null` plugin (`patches/rexglue-gpu-null-plugin.patch`) is a GPU
  plugin, but "without a presenter", so it falls in neither case: there is no drawer.

With Xenos the dialog exists and the presenter draws it; in only mode it falls back to headless.
That matches what is observed.

## 3. Other affected flows: message boxes

`XamShowMessageBoxUI_entry` (`xam_ui.cpp:268`) has the same pattern: with a drawer, an ImGui
`MessageBoxDialog`; without one, it picks the active button and completes. Callers in the guest (all
through the thunk `sub_8287D8E0`; strings read from the image):

| Function | Dialog |
|---|---|
| `sub_82194A98` | "Sign In Error" (no profile), "Xbox LIVE Sign In", "Xbox LIVE Disconnected" |
| `sub_823750A0` | "Achievements Error", "Leaderboards Error" |
| `sub_8238D1E8` | "Sign In Error: This action is not available to Guests." |
| `sub_823AB2D8` | "Achievement Failed" (no storage device) |
| `sub_823AB538` | "Gamer Pic Failed" |
| `sub_823AB7A0` | "Avatar Award Failed" |
| `sub_823ABA08` | "Storage Device" (device not available, continue without saving?) |
| `sub_823ABDD0` | "Corrupt/Damaged Save" (the save must be deleted to continue), 2 buttons, active 0 |
| `sub_823AC088` | "Existing Save … Do you want to overwrite the save?", 2 buttons, active 0 |

Plus `XamShowMessageBoxUIEx` from `sub_8287EBC8`, not analyzed.

#### Which button is picked automatically in the save dialogs

The buttons of `sub_823AC088` ("Existing Save"), `sub_823ABDD0` ("Corrupt/Damaged Save") and
`sub_823ABA08` ("Storage Device") are built from the same two localization keys:
`buttons[0]` = `0x820D3F18` "No", `buttons[1]` = `0x820D3F20` "Yes" (UTF-16BE in the image;
`sub_823AC088` at `0x823AC2C4`-`0x823AC34C`). All three pass `active_button = 0`, so in only mode the
automatic answer is **"No"**. The result goes to the global `0x8355A25C` and is consumed by the
storage state machine `sub_82195AA8` (pending state in `0x8355A274`: 4 = damaged save, 5 = existing
save):

- **Corrupt/Damaged Save** (state 4, `loc_82195E98`): only with "Yes" (result 1) does it call
  `sub_8287E5D0` = `XamContentDelete`. With "No" it does not delete; it stores a flag in `0x8355A264`
  and goes on (the dialog text says saving stays disabled until the save is deleted or the device
  changes).
- **Existing Save** (state 5, `loc_82195E50`): with "Yes" it adopts the new device (`0x83068C44` →
  `0x83068C48`) and the save is overwritten; with "No" it sets the device to -1 and shows "Storage
  Device" ("Game progress can not be saved … continue without saving?"), which is also answered "No"
  automatically; that "No" leaves the device at -1 and moves the game to state 6 (`loc_82195DD0`).
  The dialog is triggered by a device change (`loc_82195F80`: the chosen device differs from the
  previous one); with the SDK's headless selector, which always returns device 1, it should not show
  up in normal use.

Conclusion: **there is no risk of deleting or overwriting saves** because of the automatic answer.
There is a **medium** risk of silently losing progress: if any of these dialogs shows up in only
mode, the game goes on with saving disabled without showing anything. The design below solves it
(the dialogs are shown and answered).

`XamShowDeviceSelectorUI` is always headless (it returns device 1), the same in both modes.
`XamShowAchievementsUI`, `XamShowMarketplaceUI`, `XamShowGamerCardUIForXUID` and
`XamShowMessageComposeUI` are stubs in the SDK, also the same in both modes.

## 4. Proposed design: "detached" ImGui on the backend

The SDK does not need patching. Everything used is public API:

- `rex::ui::ImGuiDrawer` (`include/rex/ui/imgui_drawer.h`): built with the window, it accepts
  `SetPresenterAndImmediateDrawer(nullptr, drawer)` ("presenter is nullptr in detached mode;
  ImGuiDrawer tolerates that", `rex_app.cpp:391`). `HasDialogs()` says whether there is anything to
  draw.
- `rex::ui::ImmediateDrawer` / `ImmediateTexture` (`include/rex/ui/immediate_drawer.h`): a virtual
  interface (RGBA textures, batches of triangles with texture and scissor).
- `rex::ui::UIDrawContext` (`include/rex/ui/presenter.h:79`): has a protected constructor without a
  presenter "for app-driven (detached) overlay contexts"; used through a subclass.
- `rex::Runtime::set_imgui_drawer` (`include/rex/runtime.h:150`): public.

Pieces:

1. **Backend** (`backend/`, through the C API; no OGRE outside): a final screen-space UI pass, after
   the guest frame and before present. Inputs: RGBA8 textures by id (create/destroy) and a list of
   batches (2D position + UV + RGBA8 color vertices, indices, texture, scissor), normal alpha
   blending, no depth. It works in window coordinates, not in the guest's 720p, so the letterbox does
   not affect it. All through the OGRE API.
2. **Adapter** (in `live/` or a new module next to the frontend): an `ImmediateDrawer` that does not
   draw directly but **records** the batches into a "UI frame" in host memory (outside
   `commands/`). A detached `UIDrawContext` of the window size.
3. **Thread**: `ImGuiDrawer` has no locks (`dialogs_` is modified from the UI thread when dialogs
   open and close), so `Draw` must run on the UI thread, as the SDK presenter and Nocturne do
   (`CallInUIThreadSynchronous`, `nocturnerecomp_app.h:395`). The UI thread builds the UI frame and
   publishes it; the live mode consumer draws the last published UI frame over every guest frame. A
   UI frame is only requested while `HasDialogs()`.
4. **Setup**: in only mode, when setup ends (e.g. `OnPostSetup`), create the drawer, give it the
   adapter and publish it with `runtime()->set_imgui_drawer()`. On shutdown,
   `set_imgui_drawer(nullptr)` before destroying it. In Xenos mode nothing changes (it already has a
   drawer).

With the drawer published, `XamShowKeyboardUI` and `XamShowMessageBoxUI` take the same path as with
Xenos, with the completion semantics already right (section 2). Nothing has to be reimplemented on
the XAM side.

### Keyboard, SDL and Steam Deck

- Typing: the SDK window turns `SDL_EVENT_TEXT_INPUT` into one `OnKeyChar` per codepoint
  (`src/ui/window_sdl.cpp:532`) and the `ImGuiDrawer` registers itself as a `WindowInputListener`
  while it has dialogs. Enter confirms (`ImGuiInputTextFlags_EnterReturnsTrue`).
- Activation: when ImGui wants text, `ImGuiDrawer::PlatformSetImeData` → `SetWindowTextInputActive`
  → `Window::SetTextInputActive` on the UI thread → `SDL_StartTextInput`
  (`imgui_drawer.cpp:665-680`, `window_sdl.cpp:319`). Closing the dialog turns it off. The comment in
  `window.h:318` explains why it is not left on all the time (IME and on-screen keyboard during
  gameplay).
- Steam Deck: with SDL's x11 driver (the one only mode forces), `SDL_StartTextInput` opens the Steam
  floating keyboard (`steam://open/keyboard?...&Mode=0`, single-line mode: Enter closes it) if the
  `SDL_ENABLE_STEAM_SCREEN_KEYBOARD` hint is on (`thirdparty/sdl3/src/video/x11/SDL_x11keyboard.c:850-889`,
  `SDL_x11video.c:146`; SDL 3.4.14). According to `SDL_hints.h:812-828`, Steam sets that hint as an
  environment variable for games it launches in Big Picture, i.e. in the Deck's Game Mode. No code of
  our own and no environment variables of ours are needed. The SDK does not call
  `SDL_SetTextInputArea`, so the Steam keyboard gets the window's default rectangle (Steam picks the
  position). If it covers the dialog, the app can report the area.
- In the Deck's desktop mode Steam does not set the hint: there is no automatic keyboard (Steam+X
  opens it by hand). With SDL's Wayland driver (pending Wayland task) the path is different (the
  text-input protocol); it has to be checked again then.

### Known limitations

- Gamepad: solved in v0.4 (commit `5022952`, "the gamepad drives the runtime's dialogs and does
  not reach the game"): the XAM dialogs drawn by the native backend are driven with the gamepad.
  (The SDK's `ImGuiDrawer` alone does not feed gamepad keys to ImGui; the app does.)
- Font: ImGui's default font covers ASCII. The dialog buffer is 14 UTF-8 bytes (`buffer_length + 1`),
  so an accented character takes two. Also, the game font may not have every glyph ImGui lets the
  user type. Same with Xenos.
- While a dialog is open the gamepad no longer reaches the guest (same commit).

### Alternative without ImGui (not recommended)

Replace `__imp__XamShowKeyboardUI` from the app (a strong definition, as Nocturne does with
`REX_HOOK(__imp__XamUserGetSigninState, …)` in `src/xbox_live.cpp`) or the weak thunk `sub_8287D8B0`,
and handle input with `Window::SetTextInputActive` + our own `WindowInputListener`. It works for the
keyboard, but the text being typed has to be drawn (the backend has no text) and the message boxes
stay unsolved. More code for less coverage.

## 5. Implementation guide (for the render agent)

Decided: detached ImGui design; the UI frame stays **outside** `commands/` (captures are what the
guest draws, and the Xenos reference does not include the overlay either). None of this touches the
SDK.

### SDK public API used

| What | Where | Use |
|---|---|---|
| `ReXApp::OnPostSetup()` | `include/rex/rex_app.h:105` | `TorchlightApp` already uses it for `live::Install`; the drawer is created there in only mode. |
| `ReXApp::OnShutdown()` | `rex_app.h:111` | Unpublish and destroy the drawer. |
| `ReXApp::window()`, `runtime()` | `rex_app.h:249-250` | The game's SDL window and the runtime. |
| `ReXApp::OnConfigureFonts/OnConfigureStyle` | `rex_app.h:137,144` | Pass them as callbacks to the drawer constructor (same fonts/style as with Xenos). |
| `rex::ui::ImGuiDrawer(Window*, size_t z_order, FontSetupCallback, StyleSetupCallback)` | `include/rex/ui/imgui_drawer.h:37` | The drawer. `z_order` 64 like the SDK's (`rex_app.cpp:389`). |
| `ImGuiDrawer::SetPresenterAndImmediateDrawer(nullptr, drawer)` | `imgui_drawer.h:58` | Detached mode; the font atlas is uploaded lazily on the first `Draw` (`imgui_drawer.cpp:371-377`). |
| `ImGuiDrawer::Draw(UIDrawContext&)` / `HasDialogs()` | `imgui_drawer.h:50,64` | Builds the ImGui frame and calls the `ImmediateDrawer`. Returns at once if there are no dialogs. |
| `rex::Runtime::set_imgui_drawer(ImGuiDrawer*)` | `include/rex/runtime.h:150` | Publish it: from then on `XamShowKeyboardUI`/`XamShowMessageBoxUI` leave the headless path. |
| `rex::ui::ImmediateDrawer` | `include/rex/ui/immediate_drawer.h:93-131` | To implement: `CreateTexture(w, h, filter, is_repeated, rgba)`, `Begin(ctx, space_w, space_h)`, `BeginDrawBatch(batch)`, `Draw(draw)`, `EndDrawBatch()`, `End()`. |
| `ImmediateTexture`, `ImmediateVertex {x, y, u, v, color}`, `ImmediateDrawBatch`, `ImmediateDraw` | `immediate_drawer.h:28-90` | `ImmediateVertex` is binary-compatible with `ImDrawVert` (RGBA8 color packed as `ImU32`). `ImmediateDraw` carries `count`, `index_offset`, `base_vertex`, `texture` (null = color only), and the scissor in the coordinate space. |
| `rex::ui::UIDrawContext(uint32_t w, uint32_t h)` | `include/rex/ui/presenter.h:80` | Protected constructor for detached contexts: a trivial subclass. |
| `WindowedAppContext::CallInUIThread / CallInUIThreadSynchronous` | `include/rex/ui/windowed_app_context.h:72-73` | To run `Draw` on the UI thread. |

### Threads and flow

1. **Guest/kernel**: the guest calls `XamShowKeyboardUI`. The SDK builds the `KeyboardInputDialog`
   on the guest thread (the `ImGuiDialog` constructor calls `AddDialog`, `imgui_dialog.cpp:21`) and
   defers the completion: a kernel worker does `CallInUIThreadSynchronous(dialog->Then(&fence))` and
   waits on the fence until the dialog closes (`xam_ui.cpp:124-156`). The guest keeps running and
   polling the XOVERLAPPED.
2. **UI (SDL)**: `AddDialog` registers the drawer as a `WindowInputListener` of the window; keys,
   characters (`SDL_EVENT_TEXT_INPUT` → `OnKeyChar`) and mouse/touch arrive through the UI thread's
   event loop. The focus of an `InputText` turns on `SDL_StartTextInput` (and the Steam keyboard) on
   that same thread.
3. **Who calls `Draw`**: without a presenter nobody does. Proposal: the live mode consumer, once per
   frame, queues on the UI thread (`CallInUIThread`, fire-and-forget, with an atomic flag so no more
   than one is pending) a task that:
   - if `HasDialogs()`: creates the detached `UIDrawContext` of the window size, calls
     `drawer->Draw(ctx)` and publishes the UI frame the adapter recorded;
   - otherwise: publishes an empty UI frame (so the last dialog does not stay on screen).
   `Draw` has to run on the UI thread: `ImGuiDrawer` has no locks, the input state is modified by the
   event loop on that thread, and that is where the presenter calls it with Xenos.
4. **Adapter** (`ImmediateDrawer`, in `live/` because it already links `rex::runtime`; no OGRE):
   - `CreateTexture`: assigns an id, copies the RGBA data and queues "create texture"; the
     `ImmediateTexture` it returns queues "release" in its destructor. It is called on the UI thread
     (the font atlas, on the first `Draw`).
   - `Begin`/`BeginDrawBatch`/`Draw`/`EndDrawBatch`/`End`: copy vertices, indices and commands
     (texture, scissor, index range) into a UI frame in host memory. They do not touch the backend.
   - Publishing: the finished frame and the queue of texture creations/releases go to the consumer
     through a mailbox (last frame wins; texture operations are not lost even if a frame is
     replaced).
5. **Live mode consumer** (`live/live_mode.cpp`, `RunFrameStep`): after drawing the guest frame and
   before `tl_backend_present` (the lambda around lines ~225-235), it applies the pending texture
   creations/releases and draws the last UI frame with the new C API.
6. **Backend** (new C API in `backend/backend_api.h`, implemented with the OGRE API):
   - `tl_backend_ui_texture(b, id, width, height, filter, repeat, rgba)` /
     `tl_backend_ui_release_texture(b, id)`: RGBA8 textures, separate from the guest's.
   - `tl_backend_ui_draw(b, space_w, space_h, vertices, vertex_count, indices, index_count, cmds,
     cmd_count)` with our own C types (`tl_ui_vertex` = x, y, u, v, RGBA8 color; `tl_ui_cmd` = texture
     (0 = none), first index, count, base vertex, scissor).
   - It draws on the window framebuffer (not on the guest's 720p RT), with an orthographic projection
     from ImGui's logical space (`space_w × space_h`) to the window's pixel size, `SRC_ALPHA,
     ONE_MINUS_SRC_ALPHA` blending, no depth or culling, scissor per command. No
     `xbox_to_gl_conventions` conversion applies: these are ImGui coordinates.
7. **Setup and teardown**: in `OnPostSetup`, only in only mode, create adapter + drawer +
   `SetPresenterAndImmediateDrawer(nullptr, adapter)` + `runtime()->set_imgui_drawer(drawer)`. In
   `OnShutdown`, `set_imgui_drawer(nullptr)` first, then destroy the drawer on the UI thread and
   finally the adapter. With Xenos and parallel nothing is created (the SDK already has its drawer).

### Things to check while implementing

- Mouse and touch: in only mode the backend draws in a child window inside the SDL window. Clicks
  must still reach the SDL window (the child must not select button events in X11) so OK/Cancel can
  be pressed.
- DPI: ImGui uses a logical size (`physical * medium_dpi / dpi`, `imgui_drawer.cpp:411-413`); the
  backend scales to real pixels.
- An existing race in the SDK: `AddDialog` runs on the guest thread and `Draw` on the UI thread,
  without a lock. Same with Xenos; this design does not make it worse, but it is worth knowing if a
  crash shows up when a dialog opens.
- Closing with a dialog open: the kernel worker stays waiting on the fence; same behavior as with
  Xenos.

### Validation

- Adapter unit test without a window: feed `Begin`/`BeginDrawBatch`/`Draw`/`End` with a hand-built
  batch and check the UI frame and the texture operations.
- Backend: offscreen replay of a synthetic UI frame against a reference image (outside `commands/`,
  as test data).
- In the game (agree on it first): create a character with a physical keyboard, cancel, and on the
  Deck in Game Mode with the Steam keyboard.

## 6. Future improvements

- Gamepad navigation of the dialogs: done in v0.4 (commit `5022952`).

## Decisions (2026-10-04)

1. Detached ImGui design approved; the render agent implements it (backend C API and live
   consumer).
2. The UI frame stays outside `commands/` and the captures.
3. Default button of the save dialogs verified in the code: it is "No" (non-destructive). Medium
   risk of silently unsaved progress, not of losing saves.
