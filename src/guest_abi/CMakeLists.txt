# Guest OGRE ABI map (headers only) and its compile/consistency test.
# Also configurable on its own: cmake -S src/guest_abi -B out/build/guest_abi

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.25)
    project(guest_abi LANGUAGES CXX)
    include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/guest_host_offset.cmake")
endif()

add_library(guest_abi INTERFACE)
target_include_directories(guest_abi INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(guest_abi INTERFACE cxx_std_23)

enable_testing()
add_executable(guest_abi_layout_test ogre_layout_test.cpp)
target_link_libraries(guest_abi_layout_test PRIVATE guest_abi)
target_compile_options(guest_abi_layout_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME guest_abi_layout_test COMMAND guest_abi_layout_test)
add_executable(guest_abi_mods_test mods_test.cpp)
target_link_libraries(guest_abi_mods_test PRIVATE guest_abi)
target_compile_options(guest_abi_mods_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME guest_abi_mods_test COMMAND guest_abi_mods_test)
# The mod manager's publication on a sparse 4 GiB arena (mmap): Linux only, as the achievement
# hook tests.
if(UNIX AND NOT APPLE)
  add_executable(guest_abi_mods_publish_test mods_publish_test.cpp)
  target_link_libraries(guest_abi_mods_publish_test PRIVATE guest_abi)
  target_compile_options(guest_abi_mods_publish_test PRIVATE -Wall -Wextra -Werror)
  add_test(NAME guest_abi_mods_publish_test COMMAND guest_abi_mods_publish_test)
endif()
