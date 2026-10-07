# The video settings column's model (video_menu_model.h): no guest, no OGRE; the hooks and guest
# calls (video_menu.cpp, guest_call.cpp) are part of the executable.
add_library(torchlight_game_menu_model STATIC video_menu_model.cpp menu_strings.cpp
    text_conversion.cpp wide_layout.cpp)
target_include_directories(torchlight_game_menu_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(torchlight_game_menu_model PUBLIC torchlight_settings)
target_compile_features(torchlight_game_menu_model PUBLIC cxx_std_23)
target_compile_options(torchlight_game_menu_model PRIVATE -Wall -Wextra)

add_executable(video_menu_model_test video_menu_model_test.cpp)
target_link_libraries(video_menu_model_test PRIVATE torchlight_game_menu_model)
target_compile_options(video_menu_model_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME video_menu_model_test COMMAND video_menu_model_test)

add_executable(menu_strings_test menu_strings_test.cpp)
target_link_libraries(menu_strings_test PRIVATE torchlight_game_menu_model)
target_compile_definitions(menu_strings_test PRIVATE
    TL_VIDEO_STRINGS_FILE="${CMAKE_SOURCE_DIR}/data/ui/tl_video_strings.txt")
target_compile_options(menu_strings_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME menu_strings_test COMMAND menu_strings_test)

add_executable(text_conversion_test text_conversion_test.cpp)
target_link_libraries(text_conversion_test PRIVATE torchlight_game_menu_model)
target_compile_options(text_conversion_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME text_conversion_test COMMAND text_conversion_test)

add_executable(wide_layout_test wide_layout_test.cpp)
target_link_libraries(wide_layout_test PRIVATE torchlight_game_menu_model)
target_compile_options(wide_layout_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME wide_layout_test COMMAND wide_layout_test)

# The unit save writer's mod-list repair around the original (save_mod_list.h): guest memory
# through guest_abi, no rex; the hook itself is in the executable (mods_install.cpp).
add_library(torchlight_save_mod_list STATIC save_mod_list.cpp)
target_include_directories(torchlight_save_mod_list PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(torchlight_save_mod_list PUBLIC guest_abi torchlight_mods)
target_compile_features(torchlight_save_mod_list PUBLIC cxx_std_23)
target_compile_options(torchlight_save_mod_list PRIVATE -Wall -Wextra -Werror)

add_executable(save_mod_list_test save_mod_list_test.cpp)
target_link_libraries(save_mod_list_test PRIVATE torchlight_save_mod_list)
target_compile_options(save_mod_list_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME save_mod_list_test COMMAND save_mod_list_test)
