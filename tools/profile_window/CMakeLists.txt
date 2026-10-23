# The profile window check for benchmark runs (profile_window.py): Python, tested on a log shaped
# as the game writes it.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME profile_window_test
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_profile_window.py)
endif()
