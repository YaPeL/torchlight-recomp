# The launcher (docs/launcher.md): ImGui drawn with SDL_Renderer before the runtime starts.
# ImGui itself is the SDK's, inside rex::runtime (a DLL on Windows that exports it, a static library
# on Linux); only its SDL3 backends are ours, from the same tag (third_party/imgui_backends).
set(_backends "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/imgui_backends")
add_library(torchlight_imgui_backends STATIC
    "${_backends}/imgui_impl_sdl3.cpp" "${_backends}/imgui_impl_sdlrenderer3.cpp")
target_include_directories(torchlight_imgui_backends SYSTEM PUBLIC "${_backends}")
target_include_directories(torchlight_imgui_backends PRIVATE "${TORCHLIGHT_REX_INCLUDE}")
target_link_libraries(torchlight_imgui_backends PUBLIC rex::runtime)

# One ImGui in the process: the backends work on the runtime's ImGui (versions, layout, context),
# and a frame drawn with the software renderer comes out (no window: runs without a display).
add_executable(launcher_imgui_test launcher_imgui_test.cpp)
target_link_libraries(launcher_imgui_test PRIVATE torchlight_imgui_backends torchlight_platform)
target_include_directories(launcher_imgui_test PRIVATE "${TORCHLIGHT_REX_INCLUDE}")
target_compile_options(launcher_imgui_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME launcher_imgui_test COMMAND launcher_imgui_test)
