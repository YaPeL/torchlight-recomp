# capture_dump: prints a .tlcap frame capture (needs the torchlight_commands library).
add_executable(capture_dump main.cpp)
target_link_libraries(capture_dump PRIVATE torchlight_commands)
target_compile_options(capture_dump PRIVATE -Wall -Wextra)
