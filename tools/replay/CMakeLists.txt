# replay: the frontend fed from a capture file, offscreen, compared with the reference image.
if(ANDROID)
    # A shared library whose SDL_main the activity calls (android_main.cpp), packed into a
    # development APK with tools/android/build_apk.sh.
    add_library(replay SHARED main.cpp png.cpp session_replay.cpp android_main.cpp)
    set_source_files_properties(main.cpp PROPERTIES COMPILE_DEFINITIONS main=ReplayMain)
else()
    add_executable(replay main.cpp png.cpp session_replay.cpp)
endif()
target_link_libraries(replay PRIVATE torchlight_frontend torchlight_live_mode torchlight_backend)
torchlight_stage_ogre(replay)
target_compile_options(replay PRIVATE -Wall -Wextra)

# The command line alone, without a GPU: --help prints the usage, and a bad option or no capture
# is a usage error (exit 2), not a crash.
add_test(NAME replay_help COMMAND replay --help)
set_tests_properties(replay_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: replay CAPTURE.tlcap")
add_test(NAME replay_bad_option COMMAND ${CMAKE_COMMAND} -DREPLAY=$<TARGET_FILE:replay>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/usage_error_test.cmake)
