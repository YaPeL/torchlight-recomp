# Neutral capture format (no OGRE, PPC or SDK dependencies) and its tests.
# Also configurable on its own: cmake -S src/commands -B out/build/commands

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.25)
    project(commands LANGUAGES CXX)
endif()

# A smaller host command (types.h SetGammaRamp): every target that includes commands/types.h sees
# the same layout through this PUBLIC definition. The capture format does not change. On since
# 2026-10-21 (the guest's fight 16 % faster on the POCO X7, docs/performance-profile.md); OFF
# builds the inline table again.
option(TORCHLIGHT_COMPACT_COMMANDS "Store the gamma ramp command's table on the heap" ON)
add_library(torchlight_commands STATIC serialize.cpp)
if(TORCHLIGHT_COMPACT_COMMANDS)
    target_compile_definitions(torchlight_commands PUBLIC TORCHLIGHT_COMPACT_COMMANDS)
endif()
target_include_directories(torchlight_commands PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_commands PUBLIC cxx_std_23)
target_compile_options(torchlight_commands PRIVATE -Wall -Wextra)

enable_testing()
add_executable(commands_test commands_test.cpp)
target_link_libraries(commands_test PRIVATE torchlight_commands)
target_compile_options(commands_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME commands_test COMMAND commands_test)

add_executable(command_storage_test command_storage_test.cpp)
target_link_libraries(command_storage_test PRIVATE torchlight_commands)
target_compile_options(command_storage_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME command_storage_test COMMAND command_storage_test)
# The other layout too, whichever the option: this test compiles its own copy of the serializer
# with the opposite definition, so nothing else in the build mixes the two.
add_executable(command_storage_other_layout_test command_storage_test.cpp serialize.cpp)
target_include_directories(command_storage_other_layout_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(command_storage_other_layout_test PRIVATE cxx_std_23)
if(NOT TORCHLIGHT_COMPACT_COMMANDS)
    target_compile_definitions(command_storage_other_layout_test PRIVATE TORCHLIGHT_COMPACT_COMMANDS)
endif()
target_compile_options(command_storage_other_layout_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME command_storage_other_layout_test COMMAND command_storage_other_layout_test)
