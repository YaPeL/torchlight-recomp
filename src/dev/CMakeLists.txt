# The --dev_guest_command parser (header only) and its test; the executing code is in the app
# (guest_command.cpp, behind TORCHLIGHT_DEV_COMMANDS).
add_executable(dev_command_list_test command_list_test.cpp)
target_include_directories(dev_command_list_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(dev_command_list_test PRIVATE cxx_std_23)
target_compile_options(dev_command_list_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME dev_command_list_test COMMAND dev_command_list_test)
