# The --dev_guest_command parser (header only) and its test; the executing code is in the app
# (guest_command.cpp, behind TORCHLIGHT_DEV_COMMANDS).
add_executable(dev_command_list_test command_list_test.cpp)
target_include_directories(dev_command_list_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(dev_command_list_test PRIVATE cxx_std_23)
target_compile_options(dev_command_list_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME dev_command_list_test COMMAND dev_command_list_test)

# The --dev_input_script script (input_script.h): parser and state machine, pure, with its test; the
# input driver and the game events are in the app (script_input.cpp, behind TORCHLIGHT_DEV_INPUT).
add_library(torchlight_dev_input_script STATIC input_script.cpp)
target_include_directories(torchlight_dev_input_script PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_dev_input_script PUBLIC cxx_std_23)
target_compile_options(torchlight_dev_input_script PRIVATE -Wall -Wextra)

add_executable(dev_input_script_test input_script_test.cpp)
target_link_libraries(dev_input_script_test PRIVATE torchlight_dev_input_script)
target_compile_options(dev_input_script_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME dev_input_script_test COMMAND dev_input_script_test)
