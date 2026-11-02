# The wizard on sample pages, played by a script, offscreen. Keeps its screenshots in
# wizard-shots/ of the build folder (CI uploads them). It lists only the invented folder below.
if(NOT ANDROID)
  add_executable(wizard_shots wizard_shots.cpp)
  target_link_libraries(wizard_shots PRIVATE torchlight_wizard)
  target_compile_options(wizard_shots PRIVATE -Wall -Wextra -Werror)
  set(shots "${CMAKE_BINARY_DIR}/wizard-shots")
  set(sample "${CMAKE_CURRENT_BINARY_DIR}/sample-folder")
  set(many)
  foreach(i RANGE 10 40)
    list(APPEND many "${sample}/Long list/Folder ${i}")
  endforeach()
  add_test(NAME wizard_shots_setup COMMAND ${CMAKE_COMMAND}
           -E make_directory "${shots}" "${sample}/A folder/Inside" "${sample}/Another folder" ${many})
  add_test(NAME wizard_shots_setup_file COMMAND ${CMAKE_COMMAND} -E touch "${sample}/A file.txt")
  set_tests_properties(wizard_shots_setup wizard_shots_setup_file PROPERTIES
                       FIXTURES_SETUP wizard_shots_sample)
  set(common --video offscreen --shots "${shots}" --browse_from "${sample}" --scale 1)
  add_test(NAME wizard_shots_tour COMMAND wizard_shots --script
           "${CMAKE_CURRENT_SOURCE_DIR}/tour.txt" ${common})
  add_test(NAME wizard_shots_bad_page COMMAND wizard_shots --script
           "${CMAKE_CURRENT_SOURCE_DIR}/bad_page.txt" ${common})
  # The Steam Deck's size.
  add_test(NAME wizard_shots_deck COMMAND wizard_shots --script
           "${CMAKE_CURRENT_SOURCE_DIR}/deck.txt" --width 1280 --height 800 ${common})
  set_tests_properties(wizard_shots_tour wizard_shots_bad_page wizard_shots_deck PROPERTIES
                       FIXTURES_REQUIRED wizard_shots_sample TIMEOUT 60)
  # The failure it must report, not just any non-zero exit.
  set_tests_properties(wizard_shots_bad_page PROPERTIES PASS_REGULAR_EXPRESSION "was not reached")
endif()
