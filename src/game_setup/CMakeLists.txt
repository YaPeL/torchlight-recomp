# The first start (REL.4): the game's files installed from the user's package or extracted folder.
# game_files (the table and the checks) has no runtime or platform dependency; install reads
# packages with the runtime's STFS reader; first_run is the dialogs, through the platform module.
add_library(torchlight_game_files STATIC game_files.cpp setup_text.cpp)
target_include_directories(torchlight_game_files PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_game_files PUBLIC cxx_std_23)
target_compile_options(torchlight_game_files PRIVATE -Wall -Wextra)
target_link_libraries(torchlight_game_files PRIVATE torchlight_sha256)

add_library(torchlight_game_setup STATIC install.cpp first_run.cpp)
target_link_libraries(torchlight_game_setup PUBLIC torchlight_game_files rex::runtime
                      torchlight_platform torchlight_settings torchlight_game_menu_model)
target_include_directories(torchlight_game_setup PRIVATE "${TORCHLIGHT_REX_INCLUDE}")
target_compile_options(torchlight_game_setup PRIVATE -Wall -Wextra)

add_executable(game_setup_test game_setup_test.cpp)
target_link_libraries(game_setup_test PRIVATE torchlight_game_setup)
target_compile_options(game_setup_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME game_setup_test COMMAND game_setup_test)

# Every text of the first start and the launcher has its translation in each language of tl_setup_strings.txt.
add_executable(setup_text_test setup_text_test.cpp)
target_link_libraries(setup_text_test PRIVATE torchlight_game_files torchlight_game_menu_model
                      torchlight_launcher_model)
target_compile_definitions(setup_text_test PRIVATE
    TL_SETUP_STRINGS_FILE="${CMAKE_SOURCE_DIR}/data/ui/tl_setup_strings.txt")
target_compile_options(setup_text_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME setup_text_test COMMAND setup_text_test)
