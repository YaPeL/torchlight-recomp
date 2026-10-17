# The check of the SDK's test runs (check_sdk_tests.py, for build_sdk.sh --tests and windows.ps1
# -SdkTests): Python, tested with small JUnit files.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME check_sdk_tests_test
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_check_sdk_tests.py)
endif()
