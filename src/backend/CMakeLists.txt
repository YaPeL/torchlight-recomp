# Host render backend: OGRE 14 (classic GL render system, RTSS-generated programs) behind a C API
# (backend_api.h). Callers only see the C API.
if(WIN32)
    set(TORCHLIGHT_OGRE_DEFAULT "$ENV{USERPROFILE}/ogre14-install")
elseif(ANDROID)
    # tools/deps/build_ogre.sh PREFIX WORK Release android-arm64 (static, GLES2)
    set(TORCHLIGHT_OGRE_DEFAULT "$ENV{HOME}/android-work/ogre-install")
else()
    set(TORCHLIGHT_OGRE_DEFAULT "$ENV{HOME}/ogre14-install")
endif()
set(TORCHLIGHT_OGRE_INSTALL "${TORCHLIGHT_OGRE_DEFAULT}"
    CACHE PATH "Host OGRE 14 install prefix (headers, libraries, plugins, media)")
# OGRE's own package config: its targets carry the library names and include directories of each
# platform; OGRE_PLUGIN_DIR and OGRE_MEDIA_DIR come with it. CMAKE_FIND_ROOT_PATH_BOTH: a cross
# toolchain (Android's NDK) searches packages only under its own roots, and the install is outside.
find_package(OGRE 14.6 REQUIRED CONFIG COMPONENTS RTShaderSystem
    PATHS "${TORCHLIGHT_OGRE_INSTALL}" NO_DEFAULT_PATH CMAKE_FIND_ROOT_PATH_BOTH)
if(WIN32)
    # Windows has no system zlib: tools/build-deps/windows.ps1 installs a static one into the OGRE
    # prefix, as zs.lib (zsd.lib in Debug), names FindZLIB does not look for. The frontend and the
    # live mode find it through these.
    set(ZLIB_INCLUDE_DIR "${TORCHLIGHT_OGRE_INSTALL}/include" CACHE PATH "zlib headers")
    set(ZLIB_LIBRARY_RELEASE "${TORCHLIGHT_OGRE_INSTALL}/lib/zs.lib" CACHE FILEPATH "zlib")
    set(ZLIB_LIBRARY_DEBUG "${TORCHLIGHT_OGRE_INSTALL}/lib/zsd.lib" CACHE FILEPATH "zlib, Debug")
endif()

# The render system and the image codec (ogre_plugins.h): plugin files loaded from disk, or, where
# OGRE is built static (Android, where OGRE builds only static libraries), linked in and registered
# in code. Static: the GLES2 render system and STBI, the plugins tools/deps/build_ogre.sh builds.
if(OGRE_STATIC)
    foreach(_plugin RenderSystem_GLES2 Codec_STBI)
        if(NOT TARGET ${_plugin})
            message(FATAL_ERROR "The static OGRE in ${TORCHLIGHT_OGRE_INSTALL} has no ${_plugin}")
        endif()
    endforeach()
    find_package(ZLIB REQUIRED)  # Codec_STBI's link interface names ZLIB::ZLIB
    set(_ogre_plugins_source ogre_plugins_static.cpp)
    set(_ogre_plugin_libraries RenderSystem_GLES2 Codec_STBI)
else()
    set(_ogre_plugins_source ogre_plugins_dynamic.cpp)
    set(_ogre_plugin_libraries "")
endif()
if(ANDROID)
    # OGRE's Android build links the NDK's cpufeatures (android_getCpuFeatures) only in its own build
    # tree ($<BUILD_INTERFACE:cpufeatures>), so the installed OgreMain lacks it: built here from the
    # NDK's source and added to OgreMain's link interface, for whatever links OGRE.
    add_library(torchlight_ndk_cpufeatures STATIC
        "${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c")
    target_link_libraries(torchlight_ndk_cpufeatures PRIVATE dl)
    set_property(TARGET OgreMain APPEND PROPERTY INTERFACE_LINK_LIBRARIES torchlight_ndk_cpufeatures)
endif()

# OGRE's window giving up and taking back its surface (ogre_window_surface.h): Android's EGL window.
if(ANDROID)
    set(_window_surface_source ogre_window_surface_android.cpp)
else()
    set(_window_surface_source ogre_window_surface_none.cpp)
endif()

add_library(torchlight_backend OBJECT backend.cpp ${_ogre_plugins_source} ${_window_surface_source}
            xbox_to_gl_conventions.cpp program_list.cpp)
target_include_directories(torchlight_backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_backend PRIVATE cxx_std_23)
target_compile_options(torchlight_backend PRIVATE -Wall -Wextra -Wno-deprecated-declarations)
target_link_libraries(torchlight_backend PUBLIC OgreMain OgreRTShaderSystem ${_ogre_plugin_libraries}
                      torchlight_platform)

# OGRE's plugins and the media the backend uses, as the backend finds them at run time: next to the
# executable, in ogre/plugins/ (the Wayland GL build in its wayland/ subdirectory) and ogre/media/
# (platform::OgrePluginDir, OgreMediaDir). Every executable that creates a backend gets them in the
# build tree, as links to the OGRE install; the install rules copy the files.
# OGRE loads a plugin by its versioned file name (RenderSystem_GL3Plus.so.14.6 on Linux), the file
# the unversioned name links to. macOS: the plugins are dylibs, and OGRE loads them by the
# unversioned name (OgreDynLib.cpp adds ".dylib"), so that is the name staged.
if(APPLE)
    set(_plugin_suffix "${CMAKE_SHARED_LIBRARY_SUFFIX}")
else()
    set(_plugin_suffix "${CMAKE_SHARED_MODULE_SUFFIX}")
endif()
set(_plugins "")
set(_plugin_names RenderSystem_GL3Plus Codec_STBI)
if(OGRE_STATIC)
    set(_plugin_names "")  # linked in (above): no plugin files
endif()
if(WIN32 AND EXISTS "${OGRE_PLUGIN_DIR}/RenderSystem_Direct3D11.dll")
    # The Direct3D 11 render system (tools/build-deps/windows.ps1), chosen per run.
    list(APPEND _plugin_names RenderSystem_Direct3D11)
endif()
foreach(_plugin IN LISTS _plugin_names)
    if(APPLE)
        set(_file "${OGRE_PLUGIN_DIR}/${_plugin}${_plugin_suffix}")
    else()
        file(REAL_PATH "${OGRE_PLUGIN_DIR}/${_plugin}${_plugin_suffix}" _file)
    endif()
    list(APPEND _plugins "${_file}")
    # Windows: OGRE's Debug build loads the "_d" plugins (OgreDynLib.cpp adds the suffix itself).
    if(WIN32 AND EXISTS "${OGRE_PLUGIN_DIR}/${_plugin}_d.dll")
        list(APPEND _plugins "${OGRE_PLUGIN_DIR}/${_plugin}_d.dll")
    endif()
endforeach()
set(TORCHLIGHT_OGRE_PLUGINS "${_plugins}" CACHE INTERNAL "")
set(TORCHLIGHT_OGRE_WAYLAND_PLUGIN "")
if(EXISTS "${OGRE_PLUGIN_DIR}/wayland/RenderSystem_GL3Plus${CMAKE_SHARED_MODULE_SUFFIX}")
    file(REAL_PATH "${OGRE_PLUGIN_DIR}/wayland/RenderSystem_GL3Plus${CMAKE_SHARED_MODULE_SUFFIX}"
         TORCHLIGHT_OGRE_WAYLAND_PLUGIN)
endif()
set(TORCHLIGHT_OGRE_WAYLAND_PLUGIN "${TORCHLIGHT_OGRE_WAYLAND_PLUGIN}" CACHE INTERNAL "")
set(TORCHLIGHT_OGRE_MEDIA "${OGRE_MEDIA_DIR}/Main" "${OGRE_MEDIA_DIR}/RTShaderLib" CACHE INTERNAL "")

# OGRE's libraries in the install's lib/ (macOS: the bundle's Frameworks/; the rest of the install
# rules: the top CMakeLists.txt; OGRE's targets are only visible here).
if(OGRE_STATIC)
    # Linked into the game: nothing to install.
elseif(UNIX AND NOT APPLE)
    install(FILES "$<TARGET_FILE:OgreMain>" "$<TARGET_FILE:OgreRTShaderSystem>" DESTINATION lib)
elseif(APPLE)
    install(FILES "$<TARGET_FILE:OgreMain>" "$<TARGET_FILE:OgreRTShaderSystem>"
            DESTINATION Frameworks)
endif()

function(torchlight_stage_ogre target)
    if(WIN32)
        # Windows: every executable shares one directory (the top CMakeLists.txt), so the files are
        # staged there once, as copies (symbolic links need privileges on Windows).
        if(NOT TARGET torchlight_ogre_files)
            set(_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
            set(_commands COMMAND ${CMAKE_COMMAND} -E make_directory "${_dir}/ogre/plugins"
                "${_dir}/ogre/media")
            foreach(_file IN LISTS TORCHLIGHT_OGRE_PLUGINS)
                list(APPEND _commands COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_file}"
                     "${_dir}/ogre/plugins/")
            endforeach()
            foreach(_media IN LISTS TORCHLIGHT_OGRE_MEDIA)
                get_filename_component(_name "${_media}" NAME)
                list(APPEND _commands COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                     "${_media}" "${_dir}/ogre/media/${_name}")
            endforeach()
            add_custom_target(torchlight_ogre_files ${_commands} VERBATIM)
        endif()
        add_dependencies(${target} torchlight_ogre_files)
        return()
    endif()
    get_target_property(_dir ${target} BINARY_DIR)  # the executable's directory
    set(_stamp "${_dir}/ogre.stamp")
    set(_commands
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_dir}/ogre/plugins/wayland" "${_dir}/ogre/media")
    foreach(_file IN LISTS TORCHLIGHT_OGRE_PLUGINS)
        get_filename_component(_name "${_file}" NAME)
        list(APPEND _commands COMMAND ${CMAKE_COMMAND} -E create_symlink "${_file}"
             "${_dir}/ogre/plugins/${_name}")
    endforeach()
    if(TORCHLIGHT_OGRE_WAYLAND_PLUGIN)
        get_filename_component(_name "${TORCHLIGHT_OGRE_WAYLAND_PLUGIN}" NAME)
        list(APPEND _commands COMMAND ${CMAKE_COMMAND} -E create_symlink
             "${TORCHLIGHT_OGRE_WAYLAND_PLUGIN}" "${_dir}/ogre/plugins/wayland/${_name}")
    endif()
    foreach(_media IN LISTS TORCHLIGHT_OGRE_MEDIA)
        get_filename_component(_name "${_media}" NAME)
        list(APPEND _commands COMMAND ${CMAKE_COMMAND} -E create_symlink "${_media}"
             "${_dir}/ogre/media/${_name}")
    endforeach()
    add_custom_command(OUTPUT "${_stamp}" ${_commands}
        COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
        VERBATIM)
    add_custom_target(${target}_ogre DEPENDS "${_stamp}")
    add_dependencies(${target} ${target}_ogre)
endfunction()

# The OGRE-free parts of the conventions module, tested on their own.
add_executable(conventions_test conventions_test.cpp xbox_to_gl_conventions.cpp)
target_include_directories(conventions_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_options(conventions_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME conventions_test COMMAND conventions_test)

# The list of generated programs (program_list.h), tested on its own.
add_executable(program_list_test program_list_test.cpp program_list.cpp)
target_include_directories(program_list_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(program_list_test PRIVATE cxx_std_23)
target_compile_options(program_list_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME program_list_test COMMAND program_list_test)

# The tests below draw with the render system of their command line (test_render_system.h)
# and skip (code 77) without a display. Each runs with GL3+ everywhere and, on Windows, also with
# Direct3D 11 (`<name>_d3d11`). GL3+ needs OpenGL 3.3: Windows CI and release runners have no GPU,
# only Windows' own OpenGL 1.1, on which OGRE crashes before the test can tell (no display to probe
# first, unlike Linux), and the macOS runners' virtual GPU gives no OpenGL 3 context (the backend
# reports it and the test fails), so the GL3+ runs carry the label `opengl33` and the Windows and
# macOS runners' ctest leaves it out (`-LE opengl33`, .github/workflows/ci.yml and release.yml). CI's
# Linux runner runs them under xvfb-run on Mesa's llvmpipe, in a step that fails if one is skipped.
# The Direct3D 11 runs need no GPU (Windows' software adapter, WARP) and run there too.
# The host UI pass on an offscreen backend; the internal render scale and the guest's frame size;
# a stage's constants across render target binds.
foreach(_test ui_pass_test render_scale_test draw_constants_test)
    add_executable(${_test} ${_test}.cpp)
    target_link_libraries(${_test} PRIVATE torchlight_backend)
    target_compile_options(${_test} PRIVATE -Wall -Wextra -Werror)
    torchlight_stage_ogre(${_test})
    add_test(NAME ${_test} COMMAND ${_test} --render_system=gl3plus)
    set_tests_properties(${_test} PROPERTIES SKIP_RETURN_CODE 77 LABELS opengl33)
    if(WIN32)
        add_test(NAME ${_test}_d3d11 COMMAND ${_test} --render_system=d3d11)
        set_tests_properties(${_test}_d3d11 PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endforeach()
