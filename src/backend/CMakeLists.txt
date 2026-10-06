# Host render backend: OGRE 14 (classic GL render system, RTSS-generated programs) behind a C API
# (backend_api.h). Callers only see the C API.
if(WIN32)
    set(TORCHLIGHT_OGRE_DEFAULT "$ENV{USERPROFILE}/ogre14-install")
else()
    set(TORCHLIGHT_OGRE_DEFAULT "$ENV{HOME}/ogre14-install")
endif()
set(TORCHLIGHT_OGRE_INSTALL "${TORCHLIGHT_OGRE_DEFAULT}"
    CACHE PATH "Host OGRE 14 install prefix (headers, libraries, plugins, media)")
# OGRE's own package config: its targets carry the library names and include directories of each
# platform; OGRE_PLUGIN_DIR and OGRE_MEDIA_DIR come with it.
find_package(OGRE 14.6 REQUIRED CONFIG COMPONENTS RTShaderSystem
    PATHS "${TORCHLIGHT_OGRE_INSTALL}" NO_DEFAULT_PATH)
if(WIN32)
    # Windows has no system zlib: tools/build-deps/windows.ps1 installs a static one into the OGRE
    # prefix, as zs.lib (zsd.lib in Debug), names FindZLIB does not look for. The frontend and the
    # live mode find it through these.
    set(ZLIB_INCLUDE_DIR "${TORCHLIGHT_OGRE_INSTALL}/include" CACHE PATH "zlib headers")
    set(ZLIB_LIBRARY_RELEASE "${TORCHLIGHT_OGRE_INSTALL}/lib/zs.lib" CACHE FILEPATH "zlib")
    set(ZLIB_LIBRARY_DEBUG "${TORCHLIGHT_OGRE_INSTALL}/lib/zsd.lib" CACHE FILEPATH "zlib, Debug")
endif()

add_library(torchlight_backend OBJECT backend.cpp xbox_to_gl_conventions.cpp)
target_include_directories(torchlight_backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_backend PRIVATE cxx_std_23)
target_compile_options(torchlight_backend PRIVATE -Wall -Wextra -Wno-deprecated-declarations)
target_link_libraries(torchlight_backend PUBLIC OgreMain OgreRTShaderSystem torchlight_platform)

# OGRE's plugins and the media the backend uses, as the backend finds them at run time: next to the
# executable, in ogre/plugins/ (the Wayland GL build in its wayland/ subdirectory) and ogre/media/
# (platform::OgrePluginDir, OgreMediaDir). Every executable that creates a backend gets them in the
# build tree, as links to the OGRE install; the install rules copy the files.
# OGRE loads a plugin by its versioned file name (RenderSystem_GL3Plus.so.14.6 on Linux), the file
# the unversioned name links to.
set(_plugins "")
set(_plugin_names RenderSystem_GL3Plus Codec_STBI)
if(WIN32 AND EXISTS "${OGRE_PLUGIN_DIR}/RenderSystem_Direct3D11.dll")
    # The Direct3D 11 render system (tools/build-deps/windows.ps1), chosen per run.
    list(APPEND _plugin_names RenderSystem_Direct3D11)
endif()
foreach(_plugin IN LISTS _plugin_names)
    file(REAL_PATH "${OGRE_PLUGIN_DIR}/${_plugin}${CMAKE_SHARED_MODULE_SUFFIX}" _file)
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

# OGRE's libraries in the install's lib/ (the rest of the install rules: the top CMakeLists.txt;
# OGRE's targets are only visible here).
if(UNIX AND NOT APPLE)
    install(FILES "$<TARGET_FILE:OgreMain>" "$<TARGET_FILE:OgreRTShaderSystem>" DESTINATION lib)
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

# The two tests below draw with OGRE's GL3+ render system: they skip (code 77) without a display,
# and need OpenGL 3.3. Label `opengl33`: Windows CI and release runners have no GPU, only Windows'
# own OpenGL 1.1, on which OGRE crashes before the test can tell (no display to probe first, unlike
# Linux), so their ctest leaves the label out (`-LE opengl33`, .github/workflows/ci.yml and
# release.yml). They run on machines with a GPU.
# The host UI pass on an offscreen backend.
add_executable(ui_pass_test ui_pass_test.cpp)
target_link_libraries(ui_pass_test PRIVATE torchlight_backend)
target_compile_options(ui_pass_test PRIVATE -Wall -Wextra -Werror)
torchlight_stage_ogre(ui_pass_test)
add_test(NAME ui_pass_test COMMAND ui_pass_test)
set_tests_properties(ui_pass_test PROPERTIES SKIP_RETURN_CODE 77 LABELS opengl33)

add_executable(render_scale_test render_scale_test.cpp)
target_link_libraries(render_scale_test PRIVATE torchlight_backend)
target_compile_options(render_scale_test PRIVATE -Wall -Wextra -Werror)
torchlight_stage_ogre(render_scale_test)
add_test(NAME render_scale_test COMMAND render_scale_test)
set_tests_properties(render_scale_test PROPERTIES SKIP_RETURN_CODE 77 LABELS opengl33)
