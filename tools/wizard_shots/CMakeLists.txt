# The wizard on sample pages, played by a script, offscreen. Keeps its screenshots in
# wizard-shots/ of the build folder (CI uploads them). It lists only the invented folder below.
if(NOT ANDROID)
  add_executable(wizard_shots wizard_shots.cpp)
  target_link_libraries(wizard_shots PRIVATE torchlight_wizard torchlight_game_setup)
  target_compile_options(wizard_shots PRIVATE -Wall -Wextra -Werror)
  set(shots "${CMAKE_BINARY_DIR}/wizard-shots")
  set(sample "${CMAKE_CURRENT_BINARY_DIR}/sample-folder")
  set(many)
  foreach(i RANGE 10 40)
    list(APPEND many "${sample}/Long list/Folder ${i}")
  endforeach()
  add_test(NAME wizard_shots_setup COMMAND ${CMAKE_COMMAND}
           -E make_directory "${shots}" "${shots}/es" "${shots}/de" "${shots}/fr"
           "${shots}/windows" "${shots}/macos" "${shots}/linux"
           "${sample}/A folder/Inside" "${sample}/Another folder" ${many})
  add_test(NAME wizard_shots_setup_file COMMAND ${CMAKE_COMMAND} -E touch "${sample}/A file.txt")
  set_tests_properties(wizard_shots_setup wizard_shots_setup_file PROPERTIES
                       FIXTURES_SETUP wizard_shots_sample)
  # The file goes into a folder the first one makes: never before it under ctest -j.
  set_tests_properties(wizard_shots_setup_file PROPERTIES DEPENDS wizard_shots_setup)
  set(common --video offscreen --shots "${shots}" --browse_from "${sample}" --scale 1)
  add_test(NAME wizard_shots_tour COMMAND wizard_shots --script
           "${CMAKE_CURRENT_SOURCE_DIR}/tour.txt" ${common})
  add_test(NAME wizard_shots_bad_page COMMAND wizard_shots --script
           "${CMAKE_CURRENT_SOURCE_DIR}/bad_page.txt" ${common})
  # The first start's real pages, each over its own invented files.
  set(setup --pages setup --video offscreen --scale 1)
  add_test(NAME wizard_shots_setup_tour COMMAND wizard_shots ${setup}
           --script "${CMAKE_CURRENT_SOURCE_DIR}/setup_tour.txt" --shots "${shots}"
           --sample_root "${CMAKE_CURRENT_BINARY_DIR}/setup-tour" --install_frames 12 --expect installed)
  add_test(NAME wizard_shots_setup_cancel COMMAND wizard_shots ${setup}
           --script "${CMAKE_CURRENT_SOURCE_DIR}/setup_cancel.txt" --shots "${shots}"
           --sample_root "${CMAKE_CURRENT_BINARY_DIR}/setup-cancel" --install_frames 120 --expect none)
  add_test(NAME wizard_shots_setup_package COMMAND wizard_shots ${setup}
           --script "${CMAKE_CURRENT_SOURCE_DIR}/setup_package.txt" --shots "${shots}"
           --sample_root "${CMAKE_CURRENT_BINARY_DIR}/setup-package" --install_frames 12 --expect installed)
  # As a Steam Deck with no pad (a pad on the machine running it is not counted).
  add_test(NAME wizard_shots_setup_deck COMMAND wizard_shots ${setup}
           --script "${CMAKE_CURRENT_SOURCE_DIR}/setup_deck.txt" --shots "${shots}"
           --sample_root "${CMAKE_CURRENT_BINARY_DIR}/setup-deck" --install_frames 12
           --width 1280 --height 800 --deck 1 --expect deck-closed)
  set(setup_tests wizard_shots_setup_tour wizard_shots_setup_cancel wizard_shots_setup_package
      wizard_shots_setup_deck)
  # The storage pages, over invented folders.
  foreach(case "ask|1|copied-user" "other|1|copied-other" "parent_unwritable|0|user-folders"
          "portable_unwritable|0|user-folders" "cancel|0|no-portable")
    string(REPLACE "|" ";" parts "${case}")
    list(GET parts 0 name)
    list(GET parts 1 user_data)
    list(GET parts 2 expect)
    set(mode ${name})
    if(name STREQUAL "other" OR name STREQUAL "cancel")
      set(mode ask)
    endif()
    add_test(NAME wizard_shots_storage_${name} COMMAND wizard_shots ${setup}
             --script "${CMAKE_CURRENT_SOURCE_DIR}/storage_${name}.txt" --shots "${shots}"
             --sample_root "${CMAKE_CURRENT_BINARY_DIR}/storage-${name}" --storage ${mode}
             --user_data ${user_data} --expect ${expect} --install_frames 18)
    list(APPEND setup_tests wizard_shots_storage_${name})
  endforeach()
  foreach(language es de fr)
    add_test(NAME wizard_shots_setup_${language} COMMAND wizard_shots ${setup}
             --script "${CMAKE_CURRENT_SOURCE_DIR}/setup_language.txt" --shots "${shots}/${language}"
             --sample_root "${CMAKE_CURRENT_BINARY_DIR}/setup-${language}"
             --language ${language} --strings "${CMAKE_SOURCE_DIR}/data/ui/tl_setup_strings.txt")
    list(APPEND setup_tests wizard_shots_setup_${language})
  endforeach()
  set_tests_properties(${setup_tests} PROPERTIES FIXTURES_REQUIRED wizard_shots_sample TIMEOUT 60)
  # Each operating system's explanation for an unwritable place, and the elevated warning.
  foreach(os windows macos linux)
    add_test(NAME wizard_shots_unwritable_${os} COMMAND wizard_shots ${setup}
             --script "${CMAKE_CURRENT_SOURCE_DIR}/unwritable_${os}.txt" --shots "${shots}/${os}"
             --sample_root "${CMAKE_CURRENT_BINARY_DIR}/unwritable-${os}" --storage parent_unwritable --os ${os})
    add_test(NAME wizard_shots_unwritable_folder_${os} COMMAND wizard_shots ${setup}
             --script "${CMAKE_CURRENT_SOURCE_DIR}/unwritable_folder_${os}.txt" --shots "${shots}/${os}"
             --sample_root "${CMAKE_CURRENT_BINARY_DIR}/unwritable-folder-${os}" --storage portable_unwritable --os ${os})
    list(APPEND setup_tests wizard_shots_unwritable_${os} wizard_shots_unwritable_folder_${os})
  endforeach()
  add_test(NAME wizard_shots_elevated COMMAND wizard_shots ${setup}
           --script "${CMAKE_CURRENT_SOURCE_DIR}/elevated_shots.txt" --shots "${shots}/windows"
           --sample_root "${CMAKE_CURRENT_BINARY_DIR}/elevated" --storage ask --elevated 1 --os windows
           --expect user-folders)
  add_test(NAME wizard_shots_elevated_large COMMAND wizard_shots ${setup}
           --script "${CMAKE_CURRENT_SOURCE_DIR}/elevated_large.txt" --shots "${shots}/windows"
           --sample_root "${CMAKE_CURRENT_BINARY_DIR}/elevated-large" --storage ask --elevated 1
           --os windows --user_data 1 --scale 1.5 --expect user-folders)
  list(APPEND setup_tests wizard_shots_elevated wizard_shots_elevated_large)
  set_tests_properties(${setup_tests} PROPERTIES FIXTURES_REQUIRED wizard_shots_sample TIMEOUT 60)
  # The Steam Deck's size.
  add_test(NAME wizard_shots_deck COMMAND wizard_shots --script
           "${CMAKE_CURRENT_SOURCE_DIR}/deck.txt" --width 1280 --height 800 ${common})
  set_tests_properties(wizard_shots_tour wizard_shots_bad_page wizard_shots_deck PROPERTIES
                       FIXTURES_REQUIRED wizard_shots_sample TIMEOUT 60)
  # The failure it must report, not just any non-zero exit.
  set_tests_properties(wizard_shots_bad_page PROPERTIES PASS_REGULAR_EXPRESSION "was not reached")
endif()
