# Resource identity (dependency-free part of the capture session) and its test. The rest of
# src/capture is compiled into the app (root CMakeLists.txt).
add_library(torchlight_registry STATIC resource_registry.cpp constant_mirror.cpp)
target_include_directories(torchlight_registry PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_registry PUBLIC cxx_std_23)
target_link_libraries(torchlight_registry PUBLIC torchlight_commands torchlight_measure)
target_compile_options(torchlight_registry PRIVATE -Wall -Wextra)

add_executable(constant_mirror_test constant_mirror_test.cpp)
target_link_libraries(constant_mirror_test PRIVATE torchlight_registry)
target_compile_options(constant_mirror_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME constant_mirror_test COMMAND constant_mirror_test)

add_executable(resource_registry_test resource_registry_test.cpp)
target_link_libraries(resource_registry_test PRIVATE torchlight_registry)
target_compile_options(resource_registry_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME resource_registry_test COMMAND resource_registry_test)

# The session's produced commands (live path), without the recompiled code: the hooks' slot table
# is stubbed in the test.
add_executable(session_test session_test.cpp session.cpp guest_readers.cpp)
target_link_libraries(session_test PRIVATE torchlight_registry torchlight_live guest_abi rex::runtime)
target_include_directories(session_test PRIVATE "${TORCHLIGHT_REX_INCLUDE}")
target_compile_options(session_test PRIVATE -Wall -Wextra)
add_test(NAME session_test COMMAND session_test)
