# The capped runner for game runs (run_capped.py): Python, tested with small child processes.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME run_capped_test
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_run_capped.py)
endif()
