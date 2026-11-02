# torchlight - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake (made by codegen from the game's XEX, never in
# the repository). TORCHLIGHT_GAME=OFF configures without it: the libraries, tests and tools only,
# for builds that have no game files (CI).

cmake_minimum_required(VERSION 3.25)
project(torchlight LANGUAGES C CXX)  # C: miniz, for the save import

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Profiling builds keep the frame pointer in every target, the recompiled game included, so that
# `perf record --call-graph fp` unwinds the guest's thread (its DWARF unwinding fails on the guest
# stack; docs/game-thread-parallelism.md). Off in every preset and release: it costs a register.
option(TORCHLIGHT_FRAME_POINTERS
    "Keep frame pointers in every target, for perf --call-graph fp (profiling builds only)" OFF)
if(TORCHLIGHT_FRAME_POINTERS)
    add_compile_options(-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer)
endif()

option(TORCHLIGHT_GAME
    "Build the game executable (needs generated/, the recompiled code); OFF: libraries, tests and tools only"
    ON)
if(TORCHLIGHT_GAME)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/generated/rexglue.cmake")
        message(FATAL_ERROR
            "generated/rexglue.cmake not found: run the ReXGlue codegen on the game's XEX first, "
            "or configure with -DTORCHLIGHT_GAME=OFF to build without the game (libraries, tests, "
            "tools).")
    endif()
    include(generated/rexglue.cmake)
else()
    # What generated/rexglue.cmake does for the SDK, without the game's code.
    find_package(rexglue 0.10.0 REQUIRED CONFIG)
    message(STATUS "Found ReXGlue SDK ${REXGLUE_VERSION_STRING} at ${rexglue_DIR} (no game)")
endif()

include(cmake/guest_host_offset.cmake)

if(WIN32)
    # Every executable next to the game's, where the runtime DLLs are (no rpath on Windows).
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    # The Microsoft C runtime marks standard C functions (fopen, sscanf, getenv) as deprecated in
    # favour of its own _s versions; the code stays standard.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    # tools/build-deps/windows.ps1 builds OGRE as Debug and RelWithDebInfo only: Release (the
    # package's build) takes its RelWithDebInfo libraries, same runtime (/MD), where an imported
    # target has no Release ones.
    set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE Release RelWithDebInfo)
    set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL MinSizeRel Release RelWithDebInfo)
endif()

# The binding check's tools (tools/android/check_guest_call_binding.py, on the game and on its
# fixtures in tests/guest_call_binding): the NDK's on Android; elsewhere LLVM's nm and objdump,
# beside the compiler or apt.llvm.org's (GNU's do not read every format the check does; Xcode's nm
# and objdump are LLVM's).
if(ANDROID)
    set(_binding_tools --nm ${CMAKE_NM} --objdump ${CMAKE_OBJDUMP})
else()
    get_filename_component(_toolchain_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_binding_nm llvm-nm llvm-nm-21)
    set(_binding_objdump llvm-objdump llvm-objdump-21)
    if(APPLE)
        list(APPEND _binding_nm nm)
        list(APPEND _binding_objdump objdump)
    endif()
    find_program(TORCHLIGHT_LLVM_NM NAMES ${_binding_nm}
                 HINTS "${_toolchain_bin}" /usr/lib/llvm-21/bin REQUIRED)
    find_program(TORCHLIGHT_LLVM_OBJDUMP NAMES ${_binding_objdump}
                 HINTS "${_toolchain_bin}" /usr/lib/llvm-21/bin REQUIRED)
    set(_binding_tools --nm ${TORCHLIGHT_LLVM_NM} --objdump ${TORCHLIGHT_LLVM_OBJDUMP})
endif()
if(TORCHLIGHT_GAME)
# Sources
set(TORCHLIGHT_SOURCES
    src/main.cpp
    src/achievements/guest_hooks.cpp
    src/achievements/runtime.cpp
    # Unlock toast in the game UI (uses the game menu's GuestCall).
    src/achievements/toast.cpp
    src/achievements/ui_strings.cpp
    # The list behind the game's Achievements button (ImGui).
    src/achievements/achievement_list.cpp
    # Development only: guest developer commands (--dev_guest_command).
    src/dev/guest_command.cpp
    # Development only: controller input scripts (--dev_input_script).
    src/dev/script_input.cpp
    # Guest RenderSystem hooks and frame capture (F9, --capture_dir).
    src/capture/guest_readers.cpp
    src/capture/install.cpp
    src/capture/session.cpp
    src/hooks/bucket_cull_hooks.cpp
    src/hooks/frustum_hooks.cpp
    src/hooks/gpu_wait_hooks.cpp
    src/hooks/guest_copy_hooks.cpp
    src/hooks/native_skeleton.cpp
    src/hooks/pixel_util_hooks.cpp
    src/hooks/wardrobe_blend_hooks.cpp
    src/hooks/adaptive_vsync_hooks.cpp
    src/hooks/guest_io_hooks.cpp
    src/hooks/render_system_hooks.cpp
    src/hooks/rtss_listener_hooks.cpp
    src/hooks/resource_hooks.cpp
    src/hooks/video_mode_hooks.cpp
    src/hooks/find_file_hooks.cpp
    # The video menu inside the game's UI (hooks on the game's menus).
    src/game_menu/guest_call.cpp
    src/game_menu/language_pack.cpp
    src/game_menu/mods_install.cpp
    # The mods' units in the game's unit index (guest calls; the format and cache are torchlight_mods).
    src/mods/unit_index_install.cpp
    src/mods/save_units_install.cpp
    src/mods/text_reader_hooks.cpp
    src/mods/save_block_hooks.cpp
    src/mods/wardrobe_hooks.cpp
    src/game_menu/class_selector.cpp
    src/game_menu/save_import_menu.cpp
    src/game_menu/save_indicator.cpp
    src/game_menu/skill_tree.cpp
    src/game_menu/video_menu.cpp
)

if(ANDROID)
    # SDLActivity loads the game as a shared library and calls its SDL_main (the SDK's main, renamed
    # by SDL_main.h).
    add_library(torchlight SHARED ${TORCHLIGHT_SOURCES})
elseif(WIN32)
    add_executable(torchlight WIN32 ${TORCHLIGHT_SOURCES})
else()
    add_executable(torchlight ${TORCHLIGHT_SOURCES})
endif()

set(TORCHLIGHT_GPU_PLUGINS xenos null)
rexglue_setup_target(torchlight GPU_PLUGINS ${TORCHLIGHT_GPU_PLUGINS})
# The host wardrobe blend loop (hooks/wardrobe_blend_hooks.cpp) relies on the generated sub_822DC6B0
# being the one it was checked on: every codegen is checked again, and the result is a header the
# hook reads. A changed function does not fail the build: the game keeps the recompiled loop and
# logs why; the wardrobe_blend_generated test fails instead (tools/wardrobe_blend/check_generated.py).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_wardrobe_blend_header "${CMAKE_CURRENT_BINARY_DIR}/generated_include/wardrobe_blend_generated.h")
add_custom_command(OUTPUT "${_wardrobe_blend_header}"
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/wardrobe_blend/check_generated.py
            ${CMAKE_SOURCE_DIR}/generated/default --header "${_wardrobe_blend_header}"
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/wardrobe_blend/check_generated.py
            ${CMAKE_SOURCE_DIR}/src/guest_abi/wardrobe.h
            ${CMAKE_SOURCE_DIR}/generated/default/codegen.build.stamp
    COMMENT "Checking the generated wardrobe build against the host blend loop"
    VERBATIM)
add_custom_target(torchlight_wardrobe_blend_check DEPENDS "${_wardrobe_blend_header}")
add_dependencies(torchlight_wardrobe_blend_check torchlight_codegen)
add_dependencies(torchlight torchlight_wardrobe_blend_check)
set_source_files_properties(src/hooks/wardrobe_blend_hooks.cpp PROPERTIES
    OBJECT_DEPENDS "${_wardrobe_blend_header}")
target_include_directories(torchlight PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated_include")
# Every link checks that the generated code's calls reach the hooks
# (tools/android/check_guest_call_binding.py): on Android the game is a shared library the SDK links
# with -Wl,-Bsymbolic-functions (patch 47); everywhere, nothing (ThinLTO did, docs/regressions.md)
# may bind a generated call to a hooked function's __imp__ body past its hook. Windows' executable
# has no symbol table: the linker writes a map the check reads.
# The game's own list: _binding_tools stays the tools only, since tests/guest_call_binding passes it
# to a script that takes each fixture's map apart and refuses --map.
set(_game_binding_tools ${_binding_tools})
if(WIN32)
    set(_binding_map "${CMAKE_CURRENT_BINARY_DIR}/torchlight.map")
    # No incremental link (link.exe's default with debug information): its jump stubs would hide
    # where a call is bound.
    target_link_options(torchlight PRIVATE "LINKER:/MAP:${_binding_map}" "LINKER:/INCREMENTAL:NO")
    list(APPEND _game_binding_tools --map ${_binding_map})
endif()
add_custom_command(TARGET torchlight POST_BUILD
    COMMAND ${Python3_EXECUTABLE} -I
            ${CMAKE_SOURCE_DIR}/tools/android/check_guest_call_binding.py
            $<TARGET_FILE:torchlight> ${_game_binding_tools}
    VERBATIM)
# The default code model for the game on Linux x86-64 (docs/guest-hot-paths.md). The SDK compiles
# its targets with -mcmodel=large ("large executable support"), which makes every call between
# guest functions a 64-bit absolute address in a register and an indirect call; the executable's
# text (67 MB) is far below the default model's 2 GB, and the Windows build already uses it. The
# later -mcmodel wins; the SDK's libraries keep theirs (code models link together).
if(UNIX AND NOT APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_options(torchlight_recomp PRIVATE -mcmodel=small)
    target_compile_options(torchlight PRIVATE -mcmodel=small)
endif()
# x86-64-v3 on Linux x86-64 (the owner's decision, 2026-11-01: the Steam Deck is Linux's target and
# its Zen 2 has AVX2 and FMA; Windows stays on x86-64-v2): the generated code and the torchlight
# target's own sources (the game's static libraries keep their flags).
# src/platform/cpu_check.cpp is compiled for baseline x86-64 and stops the game with a message on a
# CPU without x86-64-v3, before anything built for it runs. The SDK's and OGRE's libraries keep
# their own flags.
option(TORCHLIGHT_X86_64_V3 "Build the game for x86-64-v3 (Linux x86-64), with a CPU check" ON)
if(TORCHLIGHT_X86_64_V3 AND UNIX AND NOT APPLE AND NOT ANDROID AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_options(torchlight_recomp PRIVATE -march=x86-64-v3)
    target_compile_options(torchlight PRIVATE -march=x86-64-v3)
    target_sources(torchlight PRIVATE src/platform/cpu_check.cpp)
    # The file's -march comes after the target's, so it wins.
    set_source_files_properties(src/platform/cpu_check.cpp PROPERTIES
        COMPILE_OPTIONS "-march=x86-64;-mno-sse4.1;-mno-sse4.2;-mno-popcnt"
        COMPILE_DEFINITIONS TORCHLIGHT_X86_64_V3=1)
    # The skeleton port (hooks/native_skeleton.cpp) on by default here only: it gains with fma as an
    # instruction (measured on the Steam Deck and the desktop, 2026-11-01).
    set_property(SOURCE src/hooks/native_skeleton.cpp APPEND PROPERTY COMPILE_DEFINITIONS
                 TORCHLIGHT_NATIVE_SKELETON_DEFAULT=1)
endif()
# Whole-program optimisation of the generated code and the game's own (docs/perf/pgo.md): ThinLTO,
# and clang's instrumented profile-guided optimisation, generated (an instrumented build writes the
# counters, src/pgo/pgo_writer.cpp) and used (a merged .profdata). Off by default; the SDK's
# libraries get neither.
option(TORCHLIGHT_LTO "ThinLTO for the generated code and the game's own" OFF)
option(TORCHLIGHT_PGO_GENERATE "Instrument the generated code and the game's own for PGO" OFF)
set(TORCHLIGHT_PGO_USE "" CACHE FILEPATH "Merged clang profile (.profdata) to optimise with")
if(TORCHLIGHT_LTO)
    target_compile_options(torchlight_recomp PRIVATE -flto=thin)
    target_compile_options(torchlight PRIVATE -flto=thin)
    # lld reads the bitcode (Windows links with lld-link already); Apple's ld64 does ThinLTO itself
    # and Xcode has no lld.
    if(APPLE)
        target_link_options(torchlight PRIVATE -flto=thin)
    else()
        target_link_options(torchlight PRIVATE -flto=thin -fuse-ld=lld)
    endif()
endif()
if(TORCHLIGHT_PGO_GENERATE AND TORCHLIGHT_PGO_USE)
    message(FATAL_ERROR "TORCHLIGHT_PGO_GENERATE and TORCHLIGHT_PGO_USE exclude each other")
endif()
if(TORCHLIGHT_PGO_GENERATE)
    target_compile_options(torchlight_recomp PRIVATE -fprofile-instr-generate)
    target_compile_options(torchlight PRIVATE -fprofile-instr-generate)
    target_link_options(torchlight PRIVATE -fprofile-instr-generate)
    target_sources(torchlight PRIVATE src/pgo/pgo_writer.cpp)
    target_compile_definitions(torchlight PRIVATE TORCHLIGHT_PGO_GENERATE=1)
elseif(TORCHLIGHT_PGO_USE)
    # A function whose code changed since the profile keeps no profile; clang says how many of
    # each translation unit's functions did (-Wprofile-instr-out-of-date), and
    # tools/pgo/check_profile_match.py turns the build log's counts into a pass or fail.
    # The generated code changes only with a codegen or ReXGlue update, which needs a new profile:
    # a function of it whose profile no longer matches fails the build. The game's own code changes
    # every day; its stale functions only lose their profile (a warning).
    # A missing profile stops the configure, and a new one rebuilds what reads it.
    if(NOT EXISTS "${TORCHLIGHT_PGO_USE}")
        message(FATAL_ERROR "TORCHLIGHT_PGO_USE: no profile at ${TORCHLIGHT_PGO_USE}")
    endif()
    target_compile_options(torchlight_recomp PRIVATE "-fprofile-instr-use=${TORCHLIGHT_PGO_USE}"
                           -Wno-profile-instr-unprofiled -Werror=profile-instr-out-of-date)
    target_compile_options(torchlight PRIVATE "-fprofile-instr-use=${TORCHLIGHT_PGO_USE}"
                           -Wno-profile-instr-unprofiled)
    foreach(_pgo_target IN ITEMS torchlight_recomp torchlight)
        get_target_property(_pgo_sources ${_pgo_target} SOURCES)
        set_property(SOURCE ${_pgo_sources} TARGET_DIRECTORY ${_pgo_target}
                     APPEND PROPERTY OBJECT_DEPENDS "${TORCHLIGHT_PGO_USE}")
    endforeach()
endif()
# The SDK copies the GPU plugins next to the executable in a POST_BUILD step, which only runs when
# the executable relinks: after reinstalling the SDK the game kept loading the old plugins. This copy
# depends on the installed plugin files instead (a stamp per plugin: OUTPUT takes no target-dependent
# generator expressions, so it cannot name the plugin's file).
set(TORCHLIGHT_STAGED_GPU_PLUGINS)
foreach(_plugin IN LISTS TORCHLIGHT_GPU_PLUGINS)
    set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/gpu_plugin_${_plugin}.stamp")
    add_custom_command(OUTPUT "${_stamp}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:rex::gpu-${_plugin}>"
                "${CMAKE_CURRENT_BINARY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
        DEPENDS "$<TARGET_FILE:rex::gpu-${_plugin}>"
        VERBATIM)
    list(APPEND TORCHLIGHT_STAGED_GPU_PLUGINS "${_stamp}")
endforeach()
add_custom_target(torchlight_gpu_plugins ALL DEPENDS ${TORCHLIGHT_STAGED_GPU_PLUGINS})
add_dependencies(torchlight torchlight_gpu_plugins)
endif()

# ReXGlue SDK include root (the SDK package's gpu target carries no include directories).
get_filename_component(TORCHLIGHT_REX_INCLUDE "${rexglue_DIR}/../../../include" ABSOLUTE)
enable_testing()
# The project logs only through the SDK's REXLOG_* macros, never spdlog directly (on Windows the
# executable has its own, empty copy of spdlog's registry): cmake/check_no_spdlog.cmake.
add_test(NAME no_direct_spdlog COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/src
    -P ${CMAKE_SOURCE_DIR}/cmake/check_no_spdlog.cmake)
# Hooks never touch the guest registers the generated code may keep in locals (r12, r14-r31, f14-f31,
# v14 and up): cmake/check_guest_registers.cmake.
add_test(NAME no_preserved_guest_registers COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR}/src
    -P ${CMAKE_SOURCE_DIR}/cmake/check_guest_registers.cmake)
# With the game's generated code: fails when the generated sub_822DC6B0 is not the one the host
# wardrobe blend loop was checked on (the game build then keeps the recompiled loop). Labelled
# "development": the release workflow leaves it out (-LE), so a changed function there is a warning
# in the game's log, not a failed release.
if(TORCHLIGHT_GAME)
    add_test(NAME wardrobe_blend_generated COMMAND ${Python3_EXECUTABLE} -I
        ${CMAKE_SOURCE_DIR}/tools/wardrobe_blend/check_generated.py ${CMAKE_SOURCE_DIR}/generated/default)
    set_tests_properties(wardrobe_blend_generated PROPERTIES LABELS development)
endif()
add_subdirectory(src/guest_abi)
add_subdirectory(src/achievements)
add_subdirectory(src/input)
add_subdirectory(src/dev)
add_subdirectory(src/hooks)
add_subdirectory(src/commands)
add_subdirectory(src/rtss)
add_subdirectory(src/capture)
add_subdirectory(src/settings)
add_subdirectory(src/save_import)
add_subdirectory(src/mods)
add_subdirectory(src/game_menu)
add_subdirectory(src/platform)
add_subdirectory(src/backend)
add_subdirectory(src/frontend)
add_subdirectory(src/live)
add_subdirectory(src/game_setup)
if(NOT ANDROID)
  add_subdirectory(src/wizard)
endif()
add_subdirectory(tools/capture_dump)
add_subdirectory(tools/thread_profile)
add_subdirectory(tools/synthetic_capture)
add_subdirectory(tools/replay)
add_subdirectory(tools/wizard_shots)
add_subdirectory(tools/run_capped)
add_subdirectory(tools/profile_window)
add_subdirectory(tools/pgo)
add_subdirectory(tools/deps)
add_subdirectory(tools/translations)
add_subdirectory(tools/achievement_coverage)
add_subdirectory(tools/class_skill_check)
add_subdirectory(tools/release)
add_subdirectory(tests/save_convert)
add_subdirectory(tests/guest_call_binding)

if(TORCHLIGHT_GAME)
target_include_directories(torchlight PRIVATE src)
# The wardrobe blend loop's arithmetic must stay the recompiled code's: no contraction of
# floating-point operations (hooks/wardrobe_blend.h).
set_source_files_properties(src/hooks/wardrobe_blend_hooks.cpp PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
# The node and bone updates reproduce the guest's rounding (hooks/native_skeleton.cpp): no fused
# operations the guest does not do.
set_source_files_properties(src/hooks/native_skeleton.cpp PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
# Diagnostics that wrap guest functions (hooks/census_hooks.cpp, --diag_object_census;
# hooks/queue_census_hooks.cpp, --diag_queue_census; diag/store_trace.cpp, --diag_store_trace).
# Off by default and in every release workflow: without it nothing of them is compiled, no guest
# function is wrapped and nothing runs, so they cost nothing. The one build flag for such
# diagnostics.
option(TORCHLIGHT_DIAGNOSTICS "Compile the diagnostics that wrap guest functions" OFF)
if(TORCHLIGHT_DIAGNOSTICS)
    target_sources(torchlight PRIVATE src/hooks/census_hooks.cpp src/hooks/queue_census_hooks.cpp
                   src/diag/store_trace.cpp)
    # The store trace's comparison reaches into the capture session and the constants hook
    # (capture::Session::CompareLog).
    set_property(SOURCE src/capture/session.cpp src/hooks/render_system_hooks.cpp
                 src/hooks/guest_copy_hooks.cpp src/hooks/native_skeleton.cpp
                 src/diag/store_trace.cpp
                 APPEND PROPERTY COMPILE_DEFINITIONS TORCHLIGHT_DIAGNOSTICS=1)
    # The store trace (diag/store_trace.h, --diag_store_trace): precompiled after the generated
    # code's own header, it redefines its guest stores; the whole generated code is rebuilt.
    if(TARGET torchlight_recomp)
        target_precompile_headers(torchlight_recomp PRIVATE
                                  "${CMAKE_SOURCE_DIR}/src/diag/store_trace.h")
        # The kernel exports the generated code calls, wrapped by the link (GNU ld and lld:
        # Linux and Android), recorded by the store trace too.
        if(NOT WIN32 AND NOT APPLE)
            set(_st_imports_cpp "${CMAKE_CURRENT_BINARY_DIR}/store_trace_imports.cpp")
            set(_st_imports_rsp "${CMAKE_CURRENT_BINARY_DIR}/store_trace_imports.rsp")
            add_custom_command(OUTPUT "${_st_imports_cpp}" "${_st_imports_rsp}"
                COMMAND ${Python3_EXECUTABLE} -I
                        ${CMAKE_SOURCE_DIR}/tools/diag/gen_store_trace_imports.py
                        ${CMAKE_SOURCE_DIR}/generated/default "${_st_imports_cpp}"
                        "${_st_imports_rsp}"
                DEPENDS ${CMAKE_SOURCE_DIR}/tools/diag/gen_store_trace_imports.py
                        ${CMAKE_SOURCE_DIR}/generated/default/codegen.build.stamp
                VERBATIM)
            target_sources(torchlight PRIVATE "${_st_imports_cpp}")
            target_link_options(torchlight PRIVATE "@${_st_imports_rsp}")
            set_property(TARGET torchlight APPEND PROPERTY LINK_DEPENDS "${_st_imports_rsp}")
        endif()
    endif()
    target_link_libraries(torchlight PRIVATE torchlight_queue_census)
    # The colour pack/unpack checks against the guest's own functions (pixel_util_check.inc).
    # The wardrobe blend loop's check against the recompiled loop (wardrobe_blend_check.inl).
    set_source_files_properties(src/hooks/pixel_util_hooks.cpp src/hooks/wardrobe_blend_hooks.cpp PROPERTIES
        COMPILE_DEFINITIONS TORCHLIGHT_PIXEL_UTIL_DIAGNOSTICS=1)
endif()
# Guest developer commands for validation runs (dev/guest_command.h). Off by default; the release
# workflows never set it, so release builds ignore --dev_guest_command.
option(TORCHLIGHT_DEV_COMMANDS "Run guest developer commands from --dev_guest_command" OFF)
if(TORCHLIGHT_DEV_COMMANDS)
    # Only the file that uses it, so switching the option does not rebuild the recompiled code.
    set_source_files_properties(src/dev/guest_command.cpp PROPERTIES
        COMPILE_DEFINITIONS TORCHLIGHT_DEV_COMMANDS=1)
endif()
# Controller input scripts for unattended runs (dev/script_input.h). Off by default; the release
# workflows never set it, so release builds ignore --dev_input_script.
option(TORCHLIGHT_DEV_INPUT "Play controller input scripts from --dev_input_script" OFF)
if(TORCHLIGHT_DEV_INPUT)
    set_source_files_properties(src/dev/script_input.cpp PROPERTIES
        COMPILE_DEFINITIONS TORCHLIGHT_DEV_INPUT=1)
    # The mouse as a finger on the touch controls (--dev_touch_mouse).
    set_source_files_properties(src/live/touch_input.cpp DIRECTORY src/live PROPERTIES
        COMPILE_DEFINITIONS TORCHLIGHT_DEV_INPUT=1)
endif()
# Our layouts for the game's UI (game_menu/video_menu.h), next to the executable; copied on every
# build (an edited layout needs no relink).
add_custom_target(torchlight_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/ui"
            "$<TARGET_FILE_DIR:torchlight>/data/ui")
add_dependencies(torchlight torchlight_data)
# The live mode (and through it the frontend) and the backend objects.
target_link_libraries(torchlight PRIVATE torchlight_game_menu_model torchlight_mods torchlight_texture_files
    torchlight_save_mod_list torchlight_video_mode torchlight_guest_path torchlight_gpu_wait_watch torchlight_missing_file_cache
    torchlight_game_setup torchlight_save_import torchlight_guest_copy torchlight_pixel_util torchlight_bucket_cull
    torchlight_native_render_system torchlight_frustum_test torchlight_dev_input_script)
target_link_libraries(torchlight PRIVATE torchlight_commands torchlight_registry torchlight_live
    torchlight_live_mode
    torchlight_backend torchlight_achievements)

torchlight_stage_ogre(torchlight)

# The build's commit, for the first line of the log (build_info.h, rewritten only when it changes).
set(TORCHLIGHT_BUILD_INFO_DIR "${CMAKE_CURRENT_BINARY_DIR}/build_info")
add_custom_target(torchlight_build_info
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DOUTPUT=${TORCHLIGHT_BUILD_INFO_DIR}/build_info.h
            -P ${CMAKE_SOURCE_DIR}/cmake/build_info.cmake
    BYPRODUCTS "${TORCHLIGHT_BUILD_INFO_DIR}/build_info.h"
    VERBATIM)
add_dependencies(torchlight torchlight_build_info)
target_include_directories(torchlight PRIVATE "${TORCHLIGHT_BUILD_INFO_DIR}")
endif()

# Every Windows executable (game, replay, tests):
# - Windows has no rpath: they share one directory (set above), and the DLLs they link (OGRE's, the
#   SDK's; the replay links them all) are copied there once, before any of them builds;
# - the application manifest (src/platform/torchlight.manifest: UTF-8 code page, per-monitor DPI
#   awareness), merged by the linker into the manifest it embeds (a .manifest source).
function(torchlight_windows_executables dir)
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _targets)
        get_target_property(_type ${_target} TYPE)
        if(_type STREQUAL "EXECUTABLE")
            add_dependencies(${_target} torchlight_runtime_dlls)
            target_sources(${_target} PRIVATE "${CMAKE_SOURCE_DIR}/src/platform/torchlight.manifest")
        endif()
    endforeach()
    get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_subdir IN LISTS _subdirs)
        torchlight_windows_executables("${_subdir}")
    endforeach()
endfunction()
if(WIN32)
    add_custom_target(torchlight_runtime_dlls ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:replay>
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND_EXPAND_LISTS
        VERBATIM)
    torchlight_windows_executables("${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# Relocatable install (Linux): bin/ holds the executable, the GPU plugins (the SDK loads them from
# the executable's directory), data/ui and ogre/; lib/ the shared libraries, found through the
# RUNPATH $ORIGIN/../lib. In the build tree the RUNPATH has the SDK's and OGRE's library
# directories instead, so no LD_LIBRARY_PATH is needed there either.
if(UNIX AND NOT APPLE)
    set_target_properties(replay PROPERTIES
        BUILD_WITH_INSTALL_RPATH OFF
        INSTALL_RPATH "$ORIGIN/../lib")
    if(TORCHLIGHT_GAME)
        set_target_properties(torchlight PROPERTIES
            BUILD_WITH_INSTALL_RPATH OFF
            INSTALL_RPATH "$ORIGIN/../lib")
        install(TARGETS torchlight RUNTIME DESTINATION bin)
        install(FILES "$<TARGET_FILE:rex::gpu-null>" "$<TARGET_FILE:rex::gpu-xenos>"
                DESTINATION bin)
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/ui" DESTINATION bin/data)
    endif()
    # The third-party notices the licenses require, in every package.
    install(FILES "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" DESTINATION share/doc/torchlight-recomp)
    install(FILES ${TORCHLIGHT_OGRE_PLUGINS} DESTINATION bin/ogre/plugins)
    if(TORCHLIGHT_OGRE_WAYLAND_PLUGIN)
        install(FILES "${TORCHLIGHT_OGRE_WAYLAND_PLUGIN}" DESTINATION bin/ogre/plugins/wayland)
    endif()
    install(DIRECTORY ${TORCHLIGHT_OGRE_MEDIA} DESTINATION bin/ogre/media)
    install(FILES "$<TARGET_FILE:rex::runtime>" DESTINATION lib)  # OGRE's: src/backend
    # Profiling builds of the SDK runtime link Tracy (the Release one does not).
    install(FILES "$<TARGET_FILE:rex::TracyClient>" DESTINATION lib
            CONFIGURATIONS Debug RelWithDebInfo)
    # The replay tool, for checking the installed tree without the game.
    install(TARGETS replay RUNTIME DESTINATION bin COMPONENT tools EXCLUDE_FROM_ALL)
elseif(APPLE)
    # macOS app bundle (packaging/macos/make_app.sh installs into Torchlight Recomp.app/Contents):
    # MacOS/ holds the executable and the GPU plugin (the SDK loads it from the executable's
    # directory), Frameworks/ the shared libraries, PlugIns/ogre/ OGRE's plugins, Resources/ the
    # data (data/ui, ogre/media): code signing seals code and data apart. In the build tree the game
    # finds the SDK's and OGRE's dylibs through the build RPATH (their library directories), as on
    # Linux; the SDK's own setting (@executable_path, used for the build too) only finds the SDK's
    # runtime staged next to it. Xenos is not packaged: it needs MoltenVK, which the SDK looks for
    # only where a signed bundle cannot hold it (Contents/lib), and it only serves diagnostics.
    if(TORCHLIGHT_GAME)
        set_target_properties(torchlight PROPERTIES
            BUILD_WITH_INSTALL_RPATH OFF
            INSTALL_RPATH "@executable_path/../Frameworks")
        install(TARGETS torchlight RUNTIME DESTINATION MacOS)
        install(FILES "$<TARGET_FILE:rex::gpu-null>" DESTINATION MacOS)
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/ui" DESTINATION Resources/data)
    endif()
    install(FILES "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" DESTINATION Resources)
    # The plugins under their unversioned names, which OGRE loads (src/backend), as files.
    foreach(_plugin IN LISTS TORCHLIGHT_OGRE_PLUGINS)
        get_filename_component(_name "${_plugin}" NAME)
        file(REAL_PATH "${_plugin}" _file)
        install(FILES "${_file}" DESTINATION PlugIns/ogre RENAME "${_name}")
    endforeach()
    install(DIRECTORY ${TORCHLIGHT_OGRE_MEDIA} DESTINATION Resources/ogre/media)
    install(FILES "$<TARGET_FILE:rex::runtime>" DESTINATION Frameworks)  # OGRE's: src/backend
    install(FILES "$<TARGET_FILE:rex::TracyClient>" DESTINATION Frameworks
            CONFIGURATIONS Debug RelWithDebInfo)
elseif(WIN32)
    # Windows zip (packaging/windows/make_zip.ps1): one folder, with the executable next to every DLL
    # it loads (Windows looks next to the executable; there is no rpath), the GPU plugins (the SDK
    # loads them from the executable's directory), data/ui and ogre/.
    if(TORCHLIGHT_GAME)
        install(TARGETS torchlight RUNTIME DESTINATION .)
        install(FILES $<TARGET_RUNTIME_DLLS:torchlight> DESTINATION .)
        install(FILES "$<TARGET_FILE:rex::gpu-null>" "$<TARGET_FILE:rex::gpu-xenos>"
                DESTINATION .)
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/ui" DESTINATION data)
    endif()
    # The third-party notices the licenses require, next to the executable.
    install(FILES "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" DESTINATION .)
    # OGRE's plugins: the "_d" ones only for Debug (OGRE's Debug build adds the suffix itself).
    foreach(_plugin IN LISTS TORCHLIGHT_OGRE_PLUGINS)
        if(_plugin MATCHES "_d\\.dll$")
            install(FILES "${_plugin}" DESTINATION ogre/plugins CONFIGURATIONS Debug)
        else()
            install(FILES "${_plugin}" DESTINATION ogre/plugins
                    CONFIGURATIONS Release RelWithDebInfo MinSizeRel)
        endif()
    endforeach()
    install(DIRECTORY ${TORCHLIGHT_OGRE_MEDIA} DESTINATION ogre/media)
    # The MSVC runtime next to the executable (app-local, Microsoft's Distributable Code;
    # THIRD_PARTY_NOTICES.md): the DLLs the binaries import, from the Build Tools' redistributable
    # folder, found by InstallRequiredSystemLibraries. The module only runs for cl: with clang++
    # (GNU command line, MSVC ABI) it is told the toolset and architecture the compiler simulates.
    # The C runtime (UCRT) is part of Windows 10 and later.
    block(PROPAGATE CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        set(MSVC TRUE)
        if(MSVC_VERSION GREATER_EQUAL 1930)
            set(MSVC_TOOLSET_VERSION 143)
        else()
            set(MSVC_TOOLSET_VERSION 142)
        endif()
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64")
            set(CMAKE_MSVC_ARCH arm64)
        else()
            set(CMAKE_MSVC_ARCH x64)
        endif()
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
        set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
        include(InstallRequiredSystemLibraries)
    endblock()
    set(_runtime_dlls "")
    foreach(_dll IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        get_filename_component(_name "${_dll}" NAME)
        string(TOLOWER "${_name}" _name)
        if(_name MATCHES "^(vcruntime140|vcruntime140_1|msvcp140|msvcp140_atomic_wait)\\.dll$")
            list(APPEND _runtime_dlls "${_dll}")
        endif()
    endforeach()
    list(LENGTH _runtime_dlls _count)
    if(NOT _count EQUAL 4)
        message(WARNING "MSVC runtime: found ${_count} of its 4 DLLs (${_runtime_dlls}); the zip "
                        "would need the redistributable installed")
    endif()
    install(FILES ${_runtime_dlls} DESTINATION .)
    install(TARGETS replay RUNTIME DESTINATION . COMPONENT tools EXCLUDE_FROM_ALL)
endif()

# No test writes to the user's folders: every test runs with TORCHLIGHT_TEST_USER_FOLDERS pointing
# to a folder of the build tree (platform/user_folders.h puts every user folder below it, on every
# platform), created empty before the suite and removed after it. The cleanup also fails if the
# real user folders gained anything meanwhile (cmake/test_user_folders.cmake).
set(TORCHLIGHT_TEST_HOME "${CMAKE_BINARY_DIR}/test_home")
set(TORCHLIGHT_TEST_SNAPSHOT "${CMAKE_BINARY_DIR}/test_user_folders_before.txt")
function(torchlight_collect_tests directory out)
    get_property(names DIRECTORY "${directory}" PROPERTY TESTS)
    set(tests)
    foreach(name IN LISTS names)
        list(APPEND tests "${directory}|${name}")  # a test's properties live in its directory
    endforeach()
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        torchlight_collect_tests("${subdirectory}" sub_tests)
        list(APPEND tests ${sub_tests})
    endforeach()
    set(${out} "${tests}" PARENT_SCOPE)
endfunction()
torchlight_collect_tests("${CMAKE_SOURCE_DIR}" TORCHLIGHT_ALL_TESTS)
foreach(_entry IN LISTS TORCHLIGHT_ALL_TESTS)
    string(REPLACE "|" ";" _entry "${_entry}")
    list(GET _entry 0 _directory)
    list(GET _entry 1 _test)
    set_property(TEST ${_test} DIRECTORY "${_directory}" APPEND PROPERTY
        ENVIRONMENT_MODIFICATION "TORCHLIGHT_TEST_USER_FOLDERS=set:${TORCHLIGHT_TEST_HOME}")
    set_property(TEST ${_test} DIRECTORY "${_directory}" APPEND PROPERTY
        FIXTURES_REQUIRED test_home)
endforeach()
add_test(NAME test_home_setup COMMAND ${CMAKE_COMMAND} -DMODE=setup
    -DTEST_HOME=${TORCHLIGHT_TEST_HOME} -DSNAPSHOT=${TORCHLIGHT_TEST_SNAPSHOT}
    -P ${CMAKE_SOURCE_DIR}/cmake/test_user_folders.cmake)
add_test(NAME test_home_check COMMAND ${CMAKE_COMMAND} -DMODE=check
    -DTEST_HOME=${TORCHLIGHT_TEST_HOME} -DSNAPSHOT=${TORCHLIGHT_TEST_SNAPSHOT}
    -P ${CMAKE_SOURCE_DIR}/cmake/test_user_folders.cmake)
set_tests_properties(test_home_setup PROPERTIES FIXTURES_SETUP test_home)
set_tests_properties(test_home_check PROPERTIES FIXTURES_CLEANUP test_home)
