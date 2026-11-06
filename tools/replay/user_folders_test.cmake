# replay never uses the user's real folders unless asked (platform/test_user_folders.h), run here
# without TORCHLIGHT_TEST_USER_FOLDERS (as by hand):
# MODE=help: --help reports a temporary folder of its own, gone after it exits; with
#   --real_user_folders, the real ones. No display, every platform.
# MODE=synthetic: synthetic_capture's capture replayed with HOME and the XDG folders pointing at a
#   made-up home (Linux and macOS, whose user folders follow them; Windows' known folders do not):
#   nothing appears below it, and with --real_user_folders the shader cache does. Needs a display:
#   without one it says it is skipped (the test's SKIP_REGULAR_EXPRESSION).
cmake_minimum_required(VERSION 3.25)

set(unset_all --unset=TORCHLIGHT_TEST_USER_FOLDERS)

if(MODE STREQUAL "help")
    execute_process(COMMAND ${CMAKE_COMMAND} -E env ${unset_all} "${REPLAY}" --help
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "replay --help failed (${result}): ${error}")
    endif()
    string(REGEX MATCH "This run's user folders: ([^\r\n]*)" _ "${output}")
    set(own "${CMAKE_MATCH_1}")
    if(NOT own MATCHES "TorchlightRecomp-test-")
        message(FATAL_ERROR "replay run by hand did not get folders of its own: '${own}'")
    endif()
    if(EXISTS "${own}")
        message(FATAL_ERROR "replay left its folder ${own} behind")
    endif()
    execute_process(COMMAND ${CMAKE_COMMAND} -E env ${unset_all} "${REPLAY}" --help
                            --real_user_folders
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0 OR NOT output MATCHES "This run's user folders: the real ones")
        message(FATAL_ERROR "replay --help --real_user_folders (${result}): ${output}${error}")
    endif()
    message(STATUS "replay user folders: its own (${own}), the real ones only with the flag")
elseif(MODE STREQUAL "synthetic")
    if(CMAKE_HOST_WIN32)
        message(STATUS "replay user folders: skipped (Windows' known folders ignore a made-up home)")
        return()
    endif()
    if(NOT APPLE AND "$ENV{DISPLAY}" STREQUAL "" AND "$ENV{WAYLAND_DISPLAY}" STREQUAL "")
        message(STATUS "replay user folders: skipped (no display)")
        return()
    endif()
    set(home "${WORK}/home")
    file(REMOVE_RECURSE "${WORK}")
    file(MAKE_DIRECTORY "${home}")
    execute_process(COMMAND "${SYNTHETIC}" "${WORK}/synthetic.tlcap" RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "synthetic_capture failed (${result})")
    endif()
    set(fake_home ${unset_all} --unset=XDG_CONFIG_HOME --unset=XDG_STATE_HOME
        --unset=XDG_CACHE_HOME --unset=XDG_DATA_HOME "HOME=${home}")

    function(replay out_dir)
        execute_process(COMMAND ${CMAKE_COMMAND} -E env ${fake_home} "${REPLAY}"
                                "${WORK}/synthetic.tlcap" --out "${WORK}/${out_dir}" ${ARGN}
                        WORKING_DIRECTORY "${WORK}"
                        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
        if(NOT result EQUAL 0 OR NOT EXISTS "${WORK}/${out_dir}/render.png")
            message(FATAL_ERROR "replay ${ARGN} failed (${result}): ${output}${error}")
        endif()
    endfunction()
    # Ours, below the made-up home (another library's own cache, Mesa's, may be there).
    function(ours out)
        file(GLOB_RECURSE entries LIST_DIRECTORIES true "${home}/*")
        list(FILTER entries INCLUDE REGEX "/TorchlightRecomp(/|$)")
        set(${out} "${entries}" PARENT_SCOPE)
    endfunction()

    replay(out)
    ours(found)
    if(found)
        message(FATAL_ERROR "replay run by hand wrote to the user's folders: ${found}")
    endif()
    replay(out_real --real_user_folders)
    ours(found)
    if(NOT found)
        message(FATAL_ERROR "replay --real_user_folders did not use the user's folders")
    endif()
    file(REMOVE_RECURSE "${WORK}")
    message(STATUS "replay user folders: nothing written to the user's folders by default")
else()
    message(FATAL_ERROR "MODE must be help or synthetic")
endif()
