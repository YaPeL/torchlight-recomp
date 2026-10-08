# The guest frame size for other aspect ratios (video_mode.h): no guest or SDK dependency, tested
# on its own; the hooks that use it (video_mode_hooks.cpp) are part of the executable.
add_library(torchlight_video_mode STATIC video_mode.cpp)
target_include_directories(torchlight_video_mode PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_video_mode PUBLIC cxx_std_23)
target_compile_options(torchlight_video_mode PRIVATE -Wall -Wextra)

add_executable(video_mode_test video_mode_test.cpp)
target_link_libraries(video_mode_test PRIVATE torchlight_video_mode)
target_compile_options(video_mode_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME video_mode_test COMMAND video_mode_test)

# Windows path separators for the guest's file code (guest_path.h); the hook that uses them
# (find_file_hooks.cpp) is part of the executable.
add_library(torchlight_guest_path STATIC guest_path.cpp)
target_include_directories(torchlight_guest_path PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_guest_path PUBLIC cxx_std_23)
target_compile_options(torchlight_guest_path PRIVATE -Wall -Wextra)

add_executable(guest_path_test guest_path_test.cpp)
target_link_libraries(guest_path_test PRIVATE torchlight_guest_path)
target_compile_options(guest_path_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME guest_path_test COMMAND guest_path_test)
