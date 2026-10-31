# The PGO profile match check for release builds (check_profile_match.py): Python, tested on
# made-up build logs.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME pgo_profile_match_test
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_check_profile_match.py)
endif()
