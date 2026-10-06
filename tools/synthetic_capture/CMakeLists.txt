# synthetic_capture: a .tlcap without the game, for checking a packaged replay (main.cpp).
add_executable(synthetic_capture main.cpp)
target_link_libraries(synthetic_capture PRIVATE torchlight_commands)
target_compile_options(synthetic_capture PRIVATE -Wall -Wextra)
install(TARGETS synthetic_capture RUNTIME DESTINATION bin COMPONENT tools EXCLUDE_FROM_ALL)
