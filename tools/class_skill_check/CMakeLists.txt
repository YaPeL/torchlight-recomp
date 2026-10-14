# Every class's tree skills against what the skill tree shows (check.h): the check and its tests,
# and class_skill_check, run by hand on the user's own pak and mods folder.
add_library(class_skill_check_lib STATIC check.cpp)
target_include_directories(class_skill_check_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(class_skill_check_lib PUBLIC torchlight_mods torchlight_game_menu_model)
target_compile_options(class_skill_check_lib PRIVATE -Wall -Wextra -Werror)

add_executable(class_skill_check main.cpp)
target_link_libraries(class_skill_check PRIVATE class_skill_check_lib torchlight_save_import)
target_compile_options(class_skill_check PRIVATE -Wall -Wextra -Werror)

add_executable(class_skill_check_test check_test.cpp)
target_link_libraries(class_skill_check_test PRIVATE class_skill_check_lib)
target_compile_options(class_skill_check_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME class_skill_check_test COMMAND class_skill_check_test)
