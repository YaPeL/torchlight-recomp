# torchlight - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake (made by codegen from the game's XEX, never in
# the repository). TORCHLIGHT_GAME=OFF configures without it: the libraries, tests and tools only,
# for builds that have no game files (CI).

cmake_minimum_required(VERSION 3.25)
project(torchlight LANGUAGES C CXX)  # C: miniz, for the save import

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    # Guest RenderSystem hooks and frame capture (F9, --capture_dir).
    src/capture/guest_readers.cpp
    src/capture/install.cpp
    src/capture/session.cpp
    src/hooks/gpu_wait_hooks.cpp
    src/hooks/render_system_hooks.cpp
    src/hooks/resource_hooks.cpp
    src/hooks/video_mode_hooks.cpp
    # The video menu inside the game's UI (hooks on the game's menus).
    src/game_menu/guest_call.cpp
    src/game_menu/language_pack.cpp
    src/game_menu/save_import_menu.cpp
    src/game_menu/save_indicator.cpp
    src/game_menu/video_menu.cpp
)

if(WIN32)
    add_executable(torchlight WIN32 ${TORCHLIGHT_SOURCES})
else()
    add_executable(torchlight ${TORCHLIGHT_SOURCES})
endif()

set(TORCHLIGHT_GPU_PLUGINS xenos null)
rexglue_setup_target(torchlight GPU_PLUGINS ${TORCHLIGHT_GPU_PLUGINS})
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
add_subdirectory(src/guest_abi)
add_subdirectory(src/achievements)
add_subdirectory(src/dev)
add_subdirectory(src/hooks)
add_subdirectory(src/commands)
add_subdirectory(src/rtss)
add_subdirectory(src/capture)
add_subdirectory(src/settings)
add_subdirectory(src/save_import)
add_subdirectory(src/game_menu)
add_subdirectory(src/platform)
add_subdirectory(src/backend)
add_subdirectory(src/frontend)
add_subdirectory(src/live)
add_subdirectory(src/game_setup)
add_subdirectory(tools/capture_dump)
add_subdirectory(tools/thread_profile)
add_subdirectory(tools/synthetic_capture)
add_subdirectory(tools/replay)
add_subdirectory(tools/translations)
add_subdirectory(tests/save_convert)

if(TORCHLIGHT_GAME)
target_include_directories(torchlight PRIVATE src)
# Guest developer commands for validation runs (dev/guest_command.h). Off by default; the release
# workflows never set it, so release builds ignore --dev_guest_command.
option(TORCHLIGHT_DEV_COMMANDS "Run guest developer commands from --dev_guest_command" OFF)
if(TORCHLIGHT_DEV_COMMANDS)
    # Only the file that uses it, so switching the option does not rebuild the recompiled code.
    set_source_files_properties(src/dev/guest_command.cpp PROPERTIES
        COMPILE_DEFINITIONS TORCHLIGHT_DEV_COMMANDS=1)
endif()
# Our layouts for the game's UI (game_menu/video_menu.h), next to the executable; copied on every
# build (an edited layout needs no relink).
add_custom_target(torchlight_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/ui"
            "$<TARGET_FILE_DIR:torchlight>/data/ui")
add_dependencies(torchlight torchlight_data)
# The live mode (and through it the frontend) and the backend objects.
target_link_libraries(torchlight PRIVATE torchlight_game_menu_model torchlight_video_mode
    torchlight_game_setup torchlight_save_import)
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
