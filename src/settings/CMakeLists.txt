# Host settings (host_settings.h): the model and its file, with no platform or OGRE dependencies.
add_library(torchlight_settings STATIC host_settings.cpp)
target_include_directories(torchlight_settings PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_settings PUBLIC cxx_std_23)
target_compile_options(torchlight_settings PRIVATE -Wall -Wextra)

add_executable(host_settings_test host_settings_test.cpp)
target_link_libraries(host_settings_test PRIVATE torchlight_settings)
target_compile_options(host_settings_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME host_settings_test COMMAND host_settings_test)
