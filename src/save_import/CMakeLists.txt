# Import of Torchlight PC saves (docs/saves-research.md, section 4d): the C++ port of
# tools/save_convert, with no OGRE, platform or guest dependencies.

include(FetchContent)
enable_language(C)  # miniz is C (the root project enables it too; this keeps the folder buildable alone)

# miniz (MIT), for the deflate entries of the game paks. The release zip has no CMake project of its
# own (SOURCE_SUBDIR points nowhere), so miniz.c is built here.
FetchContent_Declare(miniz
  URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
  URL_HASH SHA256=ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5
  SOURCE_SUBDIR no-cmake-project
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(miniz)
add_library(torchlight_miniz STATIC "${miniz_SOURCE_DIR}/miniz.c")
target_include_directories(torchlight_miniz SYSTEM PUBLIC "${miniz_SOURCE_DIR}")
# The paks are read through our own callback (std::filesystem paths on every platform), not miniz's
# fopen path.
target_compile_definitions(torchlight_miniz PUBLIC MINIZ_NO_STDIO)
set_target_properties(torchlight_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

# SHA-256 (zlib licence), copied from the ReXGlue SDK: third_party/sha256/README.md.
add_library(torchlight_sha256 STATIC "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/sha256/sha256.cpp")
target_include_directories(torchlight_sha256 SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/sha256")

# The save schema is shared with the Python tool: embedded from tools/save_convert at configure time
# (and again whenever the file changes).
set(SAVE_SCHEMA_JSON "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/save_convert/character_save.schema.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SAVE_SCHEMA_JSON}")
file(READ "${SAVE_SCHEMA_JSON}" save_schema_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," save_schema_bytes "${save_schema_hex}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/schema_data.cpp.new"
  "// Generated from tools/save_convert/character_save.schema.json; do not edit.\n"
  "#include <cstddef>\n"
  "extern const unsigned char kSchemaJsonBytes[] = {${save_schema_bytes}};\n"
  "extern const size_t kSchemaJsonSize = sizeof(kSchemaJsonBytes);\n")
configure_file("${CMAKE_CURRENT_BINARY_DIR}/schema_data.cpp.new"
               "${CMAKE_CURRENT_BINARY_DIR}/schema_data.cpp" COPYONLY)

add_library(torchlight_save_import STATIC
  backup_retention.cpp
  digest.cpp
  game_data.cpp
  import_message.cpp
  import_plan.cpp
  inflate.cpp
  json.cpp
  pak.cpp
  save_tree.cpp
  schema.cpp
  settings_file.cpp
  stash.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/schema_data.cpp")
target_include_directories(torchlight_save_import PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_save_import PUBLIC cxx_std_23)
target_compile_options(torchlight_save_import PRIVATE -Wall -Wextra)
target_link_libraries(torchlight_save_import PRIVATE torchlight_miniz torchlight_sha256)

add_executable(save_import_deps_test deps_test.cpp)
target_link_libraries(save_import_deps_test PRIVATE torchlight_save_import)
target_compile_options(save_import_deps_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME save_import_deps_test COMMAND save_import_deps_test)

add_executable(save_import_save_tree_test save_tree_test.cpp)
target_link_libraries(save_import_save_tree_test PRIVATE torchlight_save_import)
target_compile_options(save_import_save_tree_test PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(save_import_save_tree_test PRIVATE
  SAVE_IMPORT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../tests/save_import/fixtures")
add_test(NAME save_import_save_tree_test COMMAND save_import_save_tree_test)

add_executable(save_import_game_data_test game_data_test.cpp)
target_link_libraries(save_import_game_data_test PRIVATE torchlight_save_import)
target_compile_options(save_import_game_data_test PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(save_import_game_data_test PRIVATE
  SAVE_IMPORT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../tests/save_import/fixtures")
add_test(NAME save_import_game_data_test COMMAND save_import_game_data_test)

add_executable(save_import_stash_settings_test stash_settings_test.cpp)
target_link_libraries(save_import_stash_settings_test PRIVATE torchlight_save_import)
target_compile_options(save_import_stash_settings_test PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(save_import_stash_settings_test PRIVATE
  SAVE_IMPORT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../tests/save_import/fixtures")
add_test(NAME save_import_stash_settings_test COMMAND save_import_stash_settings_test)

add_executable(save_import_import_plan_test import_plan_test.cpp)
target_link_libraries(save_import_import_plan_test PRIVATE torchlight_save_import)
target_compile_options(save_import_import_plan_test PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(save_import_import_plan_test PRIVATE
  SAVE_IMPORT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../tests/save_import/fixtures")
add_test(NAME save_import_import_plan_test COMMAND save_import_import_plan_test)

# The message texts and their translations: the menu strings reader of src/game_menu is compiled in
# (it has no dependencies), so the test also builds with src/save_import on its own.
add_executable(save_import_import_message_test import_message_test.cpp
  "${CMAKE_CURRENT_SOURCE_DIR}/../game_menu/menu_strings.cpp")
target_link_libraries(save_import_import_message_test PRIVATE torchlight_save_import)
target_compile_options(save_import_import_message_test PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(save_import_import_message_test PRIVATE
  TL_IMPORT_STRINGS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../../data/ui/tl_import_strings.txt")
add_test(NAME save_import_import_message_test COMMAND save_import_import_message_test)

add_executable(save_import_backup_retention_test backup_retention_test.cpp)
target_link_libraries(save_import_backup_retention_test PRIVATE torchlight_save_import)
target_compile_options(save_import_backup_retention_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME save_import_backup_retention_test COMMAND save_import_backup_retention_test)
