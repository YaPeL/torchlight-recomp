# Frontend: command stream -> host backend. The one path to the backend, shared by the replay
# (frames from a capture file) and the live mode (frames from the running guest).
find_package(ZLIB REQUIRED)
add_library(torchlight_frontend STATIC frontend.cpp content_source.cpp zip_archive.cpp detile.cpp)
target_include_directories(torchlight_frontend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_frontend PUBLIC cxx_std_23)
# torchlight_backend (an OBJECT library) is linked by the executables themselves.
# No ReXGlue GPU plugin here: the SDK loads it at runtime (see detile.h).
target_link_libraries(torchlight_frontend PUBLIC torchlight_commands torchlight_rtss ZLIB::ZLIB)
target_include_directories(torchlight_frontend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../backend/..")
target_compile_options(torchlight_frontend PRIVATE -Wall -Wextra)

add_executable(detile_test detile_test.cpp)
target_link_libraries(detile_test PRIVATE torchlight_frontend rex::gpu-xenos rex::runtime)
target_include_directories(detile_test SYSTEM PRIVATE "${TORCHLIGHT_REX_INCLUDE}")
target_compile_options(detile_test PRIVATE -Wall -Wextra)
add_test(NAME detile_test COMMAND detile_test)
