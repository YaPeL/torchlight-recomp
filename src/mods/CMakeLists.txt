# The player's PC mods (docs/mods.md): the text data format, the mods folder and mods.dat, and the
# repair of a save's mod list, with no guest, OGRE or platform dependencies. The app registers the
# plan with the guest.

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.25)
  project(torchlight_mods LANGUAGES CXX)
endif()

add_library(torchlight_mods STATIC dat_text.cpp mod_list.cpp save_safety.cpp saved_mod_list.cpp
    unit_index.cpp unit_cache.cpp save_units.cpp
    save_units_notice.cpp text_encoding.cpp)
target_include_directories(torchlight_mods PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_mods PUBLIC cxx_std_23)
target_link_libraries(torchlight_mods PUBLIC torchlight_save_import)
target_compile_options(torchlight_mods PRIVATE -Wall -Wextra -Werror)
set_target_properties(torchlight_mods PROPERTIES POSITION_INDEPENDENT_CODE ON)

enable_testing()
add_executable(mods_test mods_test.cpp)
target_link_libraries(mods_test PRIVATE torchlight_mods)
target_compile_options(mods_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME mods_test COMMAND mods_test)
add_executable(mods_save_safety_test save_safety_test.cpp)
target_link_libraries(mods_save_safety_test PRIVATE torchlight_mods)
target_compile_options(mods_save_safety_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME mods_save_safety_test COMMAND mods_save_safety_test)
add_executable(mods_saved_mod_list_test saved_mod_list_test.cpp)
target_link_libraries(mods_saved_mod_list_test PRIVATE torchlight_mods)
target_compile_options(mods_saved_mod_list_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME mods_saved_mod_list_test COMMAND mods_saved_mod_list_test)
add_executable(mods_unit_index_test unit_index_test.cpp)
target_link_libraries(mods_unit_index_test PRIVATE torchlight_mods)
target_compile_options(mods_unit_index_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME mods_unit_index_test COMMAND mods_unit_index_test)
add_executable(mods_unit_cache_test unit_cache_test.cpp)
target_link_libraries(mods_unit_cache_test PRIVATE torchlight_mods)
target_compile_options(mods_unit_cache_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME mods_unit_cache_test COMMAND mods_unit_cache_test)
add_executable(mods_save_units_test save_units_test.cpp)
target_link_libraries(mods_save_units_test PRIVATE torchlight_mods)
target_compile_definitions(mods_save_units_test PRIVATE
  SAVE_IMPORT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../tests/save_import/fixtures")
target_compile_options(mods_save_units_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME mods_save_units_test COMMAND mods_save_units_test)
add_executable(mods_text_encoding_test text_encoding_test.cpp)
target_link_libraries(mods_text_encoding_test PRIVATE torchlight_mods)
target_compile_options(mods_text_encoding_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME mods_text_encoding_test COMMAND mods_text_encoding_test)
