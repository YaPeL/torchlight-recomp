# Platform module (platform.h): the plain SDL3 parts (platform_sdl.cpp) plus one implementation
# file per platform.
# The user's folders and their migration (user_folders.h): no SDL or runtime, tested on their own.
# Writes that survive a power cut (durable_file.h), the same.
include(durable_file.cmake)
if(UNIX AND NOT APPLE)
    add_library(torchlight_user_folders STATIC user_folders.cpp user_folders_linux.cpp)
    add_library(torchlight_platform STATIC platform_sdl.cpp platform_linux.cpp text_wrap.cpp)
    target_link_libraries(torchlight_platform PUBLIC rex::runtime torchlight_user_folders
                          PRIVATE X11)
elseif(WIN32)
    add_library(torchlight_user_folders STATIC user_folders.cpp user_folders_win.cpp)
    # shell32/ole32: the known folders (SHGetKnownFolderPath, CoTaskMemFree).
    target_link_libraries(torchlight_user_folders PRIVATE shell32 ole32)
    add_library(torchlight_platform STATIC platform_sdl.cpp platform_win.cpp gpu_adapters.cpp
                gl_probe_win.cpp text_wrap.cpp)
    # dxgi: the GPUs (DXGI adapters); opengl32 and gdi32: the OpenGL 3.3 probe (WGL, pixel formats).
    target_link_libraries(torchlight_platform PUBLIC rex::runtime torchlight_user_folders
                          PRIVATE dxgi opengl32 gdi32)
elseif(APPLE)
    enable_language(OBJCXX)
    add_library(torchlight_user_folders STATIC user_folders.cpp user_folders_mac.cpp)
    add_library(torchlight_platform STATIC platform_sdl.cpp platform_mac.mm text_wrap.cpp)
    # AppKit: the game window's view, OGRE's window keys; CoreGraphics: the window server session.
    target_link_libraries(torchlight_platform PUBLIC rex::runtime torchlight_user_folders
                          PRIVATE "-framework AppKit" "-framework CoreGraphics")
else()
    message(FATAL_ERROR "torchlight_platform: no implementation for this platform yet")
endif()
target_include_directories(torchlight_user_folders PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_user_folders PUBLIC cxx_std_23)
target_compile_options(torchlight_user_folders PRIVATE -Wall -Wextra)
target_include_directories(torchlight_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(torchlight_platform PRIVATE "${TORCHLIGHT_REX_INCLUDE}")
target_compile_features(torchlight_platform PUBLIC cxx_std_23)
target_compile_options(torchlight_platform PRIVATE -Wall -Wextra)

# The GPU choice rules are each platform's: Linux's (PRIME, DRI_PRIME) on machines described by
# hand; Windows' on this machine (gpu_adapters_test has the made-up ones), with the paths and the
# key reader. macOS has one GPU and no rules.
if(WIN32)
    add_executable(platform_win_test platform_win_test.cpp)
    target_link_libraries(platform_win_test PRIVATE torchlight_platform shell32 ole32 opengl32)
    target_compile_options(platform_win_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME platform_win_test COMMAND platform_win_test)
    # The application manifest every executable gets (top CMakeLists.txt): UTF-8 code page, DPI.
    add_executable(utf8_paths_test utf8_paths_test.cpp)
    target_link_libraries(utf8_paths_test PRIVATE shell32)  # CommandLineToArgvW
    target_compile_options(utf8_paths_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME utf8_paths_test COMMAND utf8_paths_test)
elseif(NOT APPLE)
    add_executable(gpu_choice_test gpu_choice_test.cpp)
    target_link_libraries(gpu_choice_test PRIVATE torchlight_platform)
    target_compile_options(gpu_choice_test PRIVATE -Wall -Wextra -Werror)
    add_test(NAME gpu_choice_test COMMAND gpu_choice_test)
endif()

# Windows' GPU choice rules over made-up DXGI adapters: pure, tested on every platform.
add_executable(gpu_adapters_test gpu_adapters_test.cpp gpu_adapters.cpp)
target_include_directories(gpu_adapters_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(gpu_adapters_test PRIVATE cxx_std_23)
target_compile_options(gpu_adapters_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME gpu_adapters_test COMMAND gpu_adapters_test)

# The first start dialogs' line breaks: pure, tested on every platform.
add_executable(text_wrap_test text_wrap_test.cpp text_wrap.cpp)
target_include_directories(text_wrap_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(text_wrap_test PRIVATE cxx_std_23)
target_compile_options(text_wrap_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME text_wrap_test COMMAND text_wrap_test)

add_executable(durable_file_test durable_file_test.cpp)
target_link_libraries(durable_file_test PRIVATE torchlight_durable_file)
target_compile_options(durable_file_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME durable_file_test COMMAND durable_file_test)

add_executable(user_folders_test user_folders_test.cpp)
target_link_libraries(user_folders_test PRIVATE torchlight_user_folders)
target_compile_options(user_folders_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME user_folders_test COMMAND user_folders_test)
