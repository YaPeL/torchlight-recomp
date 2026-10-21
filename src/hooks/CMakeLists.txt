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

# The layout props' bone evaluation deferral's decision and counts (prop_bones.h), header only.
add_executable(prop_bones_test prop_bones_test.cpp)
target_include_directories(prop_bones_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(prop_bones_test PRIVATE cxx_std_23)
target_compile_options(prop_bones_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME prop_bones_test COMMAND prop_bones_test)

# The projector textures every other frame (projector_textures.h): the choice, its control and the
# alternation, tested on their own; the hook (projector_texture_hooks.cpp) is part of the executable.
add_library(torchlight_projector_textures STATIC projector_textures.cpp)
target_include_directories(torchlight_projector_textures PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_projector_textures PUBLIC cxx_std_23)
target_compile_options(torchlight_projector_textures PRIVATE -Wall -Wextra)

add_executable(projector_textures_test projector_textures_test.cpp)
target_link_libraries(projector_textures_test PRIVATE torchlight_projector_textures)
target_compile_options(projector_textures_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME projector_textures_test COMMAND projector_textures_test)
# The graphics quality levels (quality.h, header only); the hook (quality_hooks.cpp) is part of the
# executable.
add_executable(quality_test quality_test.cpp)
target_include_directories(quality_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(quality_test PRIVATE cxx_std_23)
target_compile_options(quality_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME quality_test COMMAND quality_test)
