# A test executable run by hand, outside ctest's folders, never writes to the user's real folders
# (platform/test_user_folders.h). Runs PROBE (test_user_folders_test) without
# TORCHLIGHT_TEST_USER_FOLDERS:
# - plainly: it must get a temporary folder of its own, write its file below it, and that folder
#   must be gone after it exits;
# - with --real_user_folders: no folder of its own (the variable stays unset, the cache folder is
#   the platform's); the probe then writes nothing;
# - with the variable set (as ctest and a test's child processes have it): that folder, kept after
#   the probe exits.
cmake_minimum_required(VERSION 3.25)

function(run_probe out_variable out_cache)
    execute_process(COMMAND ${CMAKE_COMMAND} -E env --unset=TORCHLIGHT_TEST_USER_FOLDERS
                            ${ARGN}
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "the probe failed (${result}): ${output}${error}")
    endif()
    string(REGEX MATCH "variable=([^\r\n]*)" _ "${output}")
    set(${out_variable} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    string(REGEX MATCH "cache=([^\r\n]*)" _ "${output}")
    set(${out_cache} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

# Plainly: a temporary folder of its own, removed at exit.
run_probe(root cache "${PROBE}")
if(root STREQUAL "" OR NOT root MATCHES "TorchlightRecomp-test-")
    message(FATAL_ERROR "run by hand, the probe got no folder of its own (variable '${root}')")
endif()
file(TO_CMAKE_PATH "${root}" root)
string(FIND "${cache}" "${root}/" at)
if(NOT at EQUAL 0)
    message(FATAL_ERROR "the probe's cache folder ${cache} is not below its own folder ${root}")
endif()
if(EXISTS "${root}")
    message(FATAL_ERROR "the probe's own folder ${root} was left behind")
endif()

# With the flag: the real folders, nothing set.
run_probe(root cache "${PROBE}" --real_user_folders)
if(NOT root STREQUAL "" OR cache MATCHES "TorchlightRecomp-test-")
    message(FATAL_ERROR "with --real_user_folders the probe still got its own folder "
                        "(variable '${root}', cache ${cache})")
endif()

# Under ctest's variable: that folder, kept.
set(given "${WORK}/given")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${given}")
run_probe(root cache ${CMAKE_COMMAND} -E env "TORCHLIGHT_TEST_USER_FOLDERS=${given}" "${PROBE}")
file(TO_CMAKE_PATH "${root}" root)
if(NOT root STREQUAL given OR NOT EXISTS "${given}")
    message(FATAL_ERROR "with the variable set the probe used '${root}' instead of ${given}, or "
                        "removed it")
endif()
file(REMOVE_RECURSE "${WORK}")
message(STATUS "test user folders: own temporary folder by hand, the real ones only with the flag")
