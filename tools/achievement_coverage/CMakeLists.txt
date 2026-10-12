# The PC achievements' coverage summary checked against its table (coverage.py): Python, no game.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME achievement_coverage_test
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_coverage.py)
endif()
