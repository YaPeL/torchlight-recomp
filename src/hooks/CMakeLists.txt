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

# The game's adaptive vsync under the host's fps cap (adaptive_vsync.h): no guest or SDK
# dependency, tested on its own; the hook (adaptive_vsync_hooks.cpp) is part of the executable and
# the settings (live/install.cpp) set the cap.
add_library(torchlight_adaptive_vsync STATIC adaptive_vsync.cpp)
target_include_directories(torchlight_adaptive_vsync PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_adaptive_vsync PUBLIC cxx_std_23)
target_compile_options(torchlight_adaptive_vsync PRIVATE -Wall -Wextra)

add_executable(adaptive_vsync_test adaptive_vsync_test.cpp)
target_link_libraries(adaptive_vsync_test PRIVATE torchlight_adaptive_vsync)
target_compile_options(adaptive_vsync_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME adaptive_vsync_test COMMAND adaptive_vsync_test)

# The watch on the guest's waits for the GPU (gpu_wait_watch.h): no guest or SDK dependency, tested
# on its own; the hook that uses it (gpu_wait_hooks.cpp) is part of the executable.
add_library(torchlight_gpu_wait_watch STATIC gpu_wait_watch.cpp)
target_include_directories(torchlight_gpu_wait_watch PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_gpu_wait_watch PUBLIC cxx_std_23)
target_compile_options(torchlight_gpu_wait_watch PRIVATE -Wall -Wextra)

add_executable(gpu_wait_watch_test gpu_wait_watch_test.cpp)
target_link_libraries(gpu_wait_watch_test PRIVATE torchlight_gpu_wait_watch)
target_compile_options(gpu_wait_watch_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME gpu_wait_watch_test COMMAND gpu_wait_watch_test)

# The game's files that do not exist, remembered (missing_file_cache.h); the hook that uses it
# (find_file_hooks.cpp) is part of the executable.
add_library(torchlight_missing_file_cache STATIC missing_file_cache.cpp)
target_include_directories(torchlight_missing_file_cache PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_missing_file_cache PUBLIC cxx_std_23)
target_compile_options(torchlight_missing_file_cache PRIVATE -Wall -Wextra)

add_executable(missing_file_cache_test missing_file_cache_test.cpp)
target_link_libraries(missing_file_cache_test PRIVATE torchlight_missing_file_cache)
target_compile_options(missing_file_cache_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME missing_file_cache_test COMMAND missing_file_cache_test)

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

# Host implementations of render system member writes (native_render_system.h): pure over a guest
# memory base; the hooks that call them (render_system_hooks.cpp) are part of the executable.
add_library(torchlight_native_render_system STATIC native_render_system.cpp)
target_include_directories(torchlight_native_render_system PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_native_render_system PUBLIC cxx_std_23)
target_compile_options(torchlight_native_render_system PRIVATE -Wall -Wextra -Werror)

add_executable(native_render_system_test native_render_system_test.cpp)
target_link_libraries(native_render_system_test PRIVATE torchlight_native_render_system)
target_compile_options(native_render_system_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME native_render_system_test COMMAND native_render_system_test)

# Stage 0 of the native render queue (queue_census.h): the classification, pure over a guest memory
# base; the diagnostic that calls it (queue_census_hooks.cpp) is part of the executable, built
# only with TORCHLIGHT_DIAGNOSTICS.
add_library(torchlight_queue_census STATIC queue_census.cpp)
target_include_directories(torchlight_queue_census PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_queue_census PUBLIC cxx_std_23)
target_compile_options(torchlight_queue_census PRIVATE -Wall -Wextra -Werror)

add_executable(queue_census_test queue_census_test.cpp)
target_link_libraries(queue_census_test PRIVATE torchlight_queue_census)
target_compile_options(queue_census_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME queue_census_test COMMAND queue_census_test)

# The fields a SetTextureBlend record keeps (texture_blend_record.h, header only).
add_executable(texture_blend_record_test texture_blend_record_test.cpp)
target_include_directories(texture_blend_record_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(texture_blend_record_test PRIVATE cxx_std_23)
target_compile_options(texture_blend_record_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME texture_blend_record_test COMMAND texture_blend_record_test)

# Frustum::isVisible(box) as host code (frustum_test.h): pure over a guest memory base, with the
# SIMDe functions the recompiled code uses (the SDK's include root) and its SSE4.1 target, so the
# vector operations are the same ones; the hook that calls it (frustum_hooks.cpp) is part of the
# executable.
add_library(torchlight_frustum_test STATIC frustum_test.cpp)
target_include_directories(torchlight_frustum_test PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(torchlight_frustum_test SYSTEM PRIVATE "${TORCHLIGHT_REX_INCLUDE}")
target_compile_features(torchlight_frustum_test PUBLIC cxx_std_23)
target_compile_options(torchlight_frustum_test PRIVATE -Wall -Wextra -Werror)
if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_options(torchlight_frustum_test PRIVATE -msse4.1)
endif()

add_executable(frustum_test_test frustum_test_test.cpp)
target_link_libraries(frustum_test_test PRIVATE torchlight_frustum_test)
target_compile_options(frustum_test_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME frustum_test_test COMMAND frustum_test_test)

# The guest OGRE's 8-bit colour pack and unpack on the host (pixel_util.h): no guest or SDK
# dependency, tested against OGRE 1.7's own algorithm; the hooks that use it (pixel_util_hooks.cpp)
# are part of the executable.
add_library(torchlight_pixel_util STATIC pixel_util.cpp)
target_include_directories(torchlight_pixel_util PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_pixel_util PUBLIC cxx_std_23)
target_compile_options(torchlight_pixel_util PRIVATE -Wall -Wextra)

add_executable(pixel_util_test pixel_util_test.cpp)
target_link_libraries(pixel_util_test PRIVATE torchlight_pixel_util)
target_compile_options(pixel_util_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME pixel_util_test COMMAND pixel_util_test)

# The wardrobe build's blend loop on the host (wardrobe_blend.h, header only): tested against the
# recompiled loop written out instruction by instruction. No contraction of floating-point
# operations, in the test as in the hooks that use it (wardrobe_blend_hooks.cpp): the recompiled
# code fuses only its fmadds.
add_executable(wardrobe_blend_test wardrobe_blend_test.cpp)
target_link_libraries(wardrobe_blend_test PRIVATE torchlight_pixel_util)
target_compile_options(wardrobe_blend_test PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
add_test(NAME wardrobe_blend_test COMMAND wardrobe_blend_test)
