# The launcher (docs/launcher.md): ImGui drawn with SDL_Renderer before the runtime starts.
# ImGui itself is the SDK's, inside rex::runtime (a DLL on Windows that exports it, a static library
# on Linux); only its SDL3 backends are ours, from the same tag (third_party/imgui_backends).
set(_backends "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/imgui_backends")
add_library(torchlight_imgui_backends STATIC
    "${_backends}/imgui_impl_sdl3.cpp" "${_backends}/imgui_impl_sdlrenderer3.cpp")
target_include_directories(torchlight_imgui_backends SYSTEM PUBLIC "${_backends}")
target_include_directories(torchlight_imgui_backends PRIVATE "${TORCHLIGHT_REX_INCLUDE}")
target_link_libraries(torchlight_imgui_backends PUBLIC rex::runtime)

# The launcher's model and its fallback file browser: what it shows and does, without drawing,
# the platform or the runtime (folders are read with std::filesystem); tested on their own.
add_library(torchlight_launcher_model STATIC launcher_model.cpp file_browser_model.cpp
            folder_listing.cpp)
target_include_directories(torchlight_launcher_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_launcher_model PUBLIC cxx_std_23)
target_compile_options(torchlight_launcher_model PRIVATE -Wall -Wextra)
target_link_libraries(torchlight_launcher_model PUBLIC torchlight_game_files torchlight_settings)

foreach(_test launcher_model_test file_browser_model_test folder_listing_test)
    add_executable(${_test} ${_test}.cpp)
    target_link_libraries(${_test} PRIVATE torchlight_launcher_model)
    target_compile_options(${_test} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${_test} COMMAND ${_test})
endforeach()

# One ImGui in the process: the backends work on the runtime's ImGui (versions, layout, context),
# and a frame drawn with the software renderer comes out (no window: runs without a display).
add_executable(launcher_imgui_test launcher_imgui_test.cpp)
target_link_libraries(launcher_imgui_test PRIVATE torchlight_imgui_backends torchlight_platform)
target_include_directories(launcher_imgui_test PRIVATE "${TORCHLIGHT_REX_INCLUDE}")
target_compile_options(launcher_imgui_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME launcher_imgui_test COMMAND launcher_imgui_test)

# The launcher's window (platform.h, LauncherWindow) and the browser's places; on SDL's offscreen
# video driver when there is no display.
add_executable(launcher_window_test launcher_window_test.cpp)
target_link_libraries(launcher_window_test PRIVATE torchlight_platform)
target_include_directories(launcher_window_test PRIVATE "${TORCHLIGHT_REX_INCLUDE}")
target_compile_options(launcher_window_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME launcher_window_test COMMAND launcher_window_test)

# The launcher: its view (ImGui) and the controller that carries out the model's actions through
# LauncherServices (the system's, or fakes in the tests).
add_library(torchlight_launcher STATIC launcher_view.cpp launcher.cpp)
target_link_libraries(torchlight_launcher PUBLIC torchlight_launcher_model torchlight_game_setup
                      torchlight_platform)
target_include_directories(torchlight_launcher PUBLIC "${TORCHLIGHT_REX_INCLUDE}")
target_compile_options(torchlight_launcher PRIVATE -Wall -Wextra)

# The whole launcher on its window with fake services and keys sent as SDL events.
add_executable(launcher_test launcher_test.cpp)
target_link_libraries(launcher_test PRIVATE torchlight_launcher)
target_compile_options(launcher_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME launcher_test COMMAND launcher_test)

# A development preview (not installed): the real launcher with a pretend install.
add_executable(launcher_preview launcher_preview.cpp)
target_link_libraries(launcher_preview PRIVATE torchlight_launcher)
target_compile_options(launcher_preview PRIVATE -Wall -Wextra -Werror)
