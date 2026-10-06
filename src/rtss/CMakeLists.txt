# Reader of the guest's RTSS-generated programs (no OGRE, PPC or SDK dependencies) and its test.

add_library(torchlight_rtss STATIC rtss_program.cpp)
target_include_directories(torchlight_rtss PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_rtss PUBLIC cxx_std_23)
target_compile_options(torchlight_rtss PRIVATE -Wall -Wextra)

add_executable(rtss_test rtss_program_test.cpp)
target_link_libraries(rtss_test PRIVATE torchlight_rtss)
target_compile_options(rtss_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME rtss_test COMMAND rtss_test)
