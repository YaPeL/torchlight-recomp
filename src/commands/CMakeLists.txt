# Neutral capture format (no OGRE, PPC or SDK dependencies) and its tests.
# Also configurable on its own: cmake -S src/commands -B out/build/commands

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.25)
    project(commands LANGUAGES CXX)
endif()

add_library(torchlight_commands STATIC serialize.cpp)
target_include_directories(torchlight_commands PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_commands PUBLIC cxx_std_23)
target_compile_options(torchlight_commands PRIVATE -Wall -Wextra)

enable_testing()
add_executable(commands_test commands_test.cpp)
target_link_libraries(commands_test PRIVATE torchlight_commands)
target_compile_options(commands_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME commands_test COMMAND commands_test)
