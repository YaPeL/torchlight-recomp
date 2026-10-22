# The host's own controllers' pure parts: the pad state and its keystrokes (shared by the touch
# controls and the development input scripts) and the touch controls' layout, finger rules and
# visibility (docs/android-port.md, A4). The drivers that feed them are in the app
# (live/touch_input.cpp, dev/script_input.cpp).
add_library(torchlight_input STATIC pad_state.cpp touch_pad.cpp touch_visibility.cpp)
target_include_directories(torchlight_input PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
# rex's keystroke_repeat.h (header only), as the SDK's pad driver uses it.
target_include_directories(torchlight_input PUBLIC "${TORCHLIGHT_REX_INCLUDE}")
target_compile_features(torchlight_input PUBLIC cxx_std_23)
target_compile_options(torchlight_input PRIVATE -Wall -Wextra)

add_executable(touch_test touch_test.cpp)
target_link_libraries(touch_test PRIVATE torchlight_input)
target_compile_options(touch_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME touch_test COMMAND touch_test)
