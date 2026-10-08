Dear ImGui's SDL3 platform backend (`imgui_impl_sdl3`) and SDL_Renderer backend for SDL3
(`imgui_impl_sdlrenderer3`), MIT license (see LICENSE.txt), for the launcher (`docs/launcher.md`).
Copied unchanged from ImGui's tag `v1.92.5` (`backends/`, commit `6d910d5`), the version the ReXGlue
SDK builds into `rexruntime` (its `thirdparty/imgui` submodule), with ImGui's line endings
(`.gitattributes`). Only the backends: ImGui itself comes from the SDK's runtime, which does not
install them; a second copy of ImGui must not be linked (`src/launcher/launcher_imgui_test.cpp`).
