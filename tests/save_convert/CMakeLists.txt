# Save converter tools (tools/save_convert): Python, tested on synthetic saves without game files.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME save_convert_test
             COMMAND ${Python3_EXECUTABLE} -m unittest discover ${CMAKE_CURRENT_SOURCE_DIR})
endif()
