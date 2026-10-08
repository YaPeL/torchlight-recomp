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

# The guest's memory copies on the host (guest_copy.h): the routing and the copy, tested with a fake
# address space; the hooks that use it (guest_copy_hooks.cpp) are part of the executable.
add_library(torchlight_guest_copy STATIC guest_copy.cpp)
target_include_directories(torchlight_guest_copy PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(torchlight_guest_copy PRIVATE "${TORCHLIGHT_REX_INCLUDE}")
target_compile_features(torchlight_guest_copy PUBLIC cxx_std_23)
target_compile_options(torchlight_guest_copy PRIVATE -Wall -Wextra)

add_executable(guest_copy_test guest_copy_test.cpp)
target_link_libraries(guest_copy_test PRIVATE torchlight_guest_copy)
target_compile_options(guest_copy_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME guest_copy_test COMMAND guest_copy_test)

# The guest D3D skip decision (guest_d3d_skip.h, header only): which slots, in which mode.
add_executable(guest_d3d_skip_test guest_d3d_skip_test.cpp)
target_include_directories(guest_d3d_skip_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(guest_d3d_skip_test PRIVATE cxx_std_23)
target_compile_options(guest_d3d_skip_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME guest_d3d_skip_test COMMAND guest_d3d_skip_test)

# Bucket culling (bucket_cull.h): boxes, planes and the per-bucket cache, with no guest access; the
# hooks that use it (bucket_cull_hooks.cpp) are part of the executable.
add_library(torchlight_bucket_cull STATIC bucket_cull.cpp)
target_include_directories(torchlight_bucket_cull PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_bucket_cull PUBLIC cxx_std_23)
target_compile_options(torchlight_bucket_cull PRIVATE -Wall -Wextra -Werror)

add_executable(bucket_cull_test bucket_cull_test.cpp)
target_link_libraries(bucket_cull_test PRIVATE torchlight_bucket_cull)
target_compile_options(bucket_cull_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME bucket_cull_test COMMAND bucket_cull_test)
