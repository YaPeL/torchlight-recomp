# Live mode: the guest's frames fed to the frontend while the game runs.
find_package(ZLIB REQUIRED)

# Contention-measuring mutex (no dependencies: the capture registry uses it too).
add_library(torchlight_measure STATIC measured_mutex.cpp)
target_include_directories(torchlight_measure PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_measure PUBLIC cxx_std_23)
target_compile_options(torchlight_measure PRIVATE -Wall -Wextra)

add_executable(measured_mutex_test measured_mutex_test.cpp)
target_link_libraries(measured_mutex_test PRIVATE torchlight_measure)
target_compile_options(measured_mutex_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME measured_mutex_test COMMAND measured_mutex_test)

add_library(torchlight_live STATIC snapshot_store.cpp frame_queue.cpp session_file.cpp
    slow_frames.cpp guest_events.cpp log_budget.cpp frame_reclaimer.cpp busy_notice_text.cpp)
target_include_directories(torchlight_live PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_live PUBLIC cxx_std_23)
target_link_libraries(torchlight_live PUBLIC torchlight_commands torchlight_measure torchlight_texture_files
    ZLIB::ZLIB)
target_compile_options(torchlight_live PRIVATE -Wall -Wextra)

add_executable(snapshot_store_test snapshot_store_test.cpp)
target_link_libraries(snapshot_store_test PRIVATE torchlight_live)
target_compile_options(snapshot_store_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME snapshot_store_test COMMAND snapshot_store_test)

add_executable(guest_events_test guest_events_test.cpp)
target_link_libraries(guest_events_test PRIVATE torchlight_live)
target_compile_options(guest_events_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME guest_events_test COMMAND guest_events_test)

add_executable(session_file_test session_file_test.cpp)
target_link_libraries(session_file_test PRIVATE torchlight_live)
target_compile_options(session_file_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME session_file_test COMMAND session_file_test)

add_executable(frame_queue_test frame_queue_test.cpp)
target_link_libraries(frame_queue_test PRIVATE torchlight_live)
target_compile_options(frame_queue_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME frame_queue_test COMMAND frame_queue_test)

add_executable(frame_reclaimer_test frame_reclaimer_test.cpp)
target_link_libraries(frame_reclaimer_test PRIVATE torchlight_live)
target_compile_options(frame_reclaimer_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME frame_reclaimer_test COMMAND frame_reclaimer_test)

add_executable(slow_frames_test slow_frames_test.cpp)
target_link_libraries(slow_frames_test PRIVATE torchlight_live)
target_compile_options(slow_frames_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME slow_frames_test COMMAND slow_frames_test)

add_executable(log_budget_test log_budget_test.cpp)
target_link_libraries(log_budget_test PRIVATE torchlight_live)
target_compile_options(log_budget_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME log_budget_test COMMAND log_budget_test)

add_executable(busy_notice_text_test busy_notice_text_test.cpp
  "${CMAKE_CURRENT_SOURCE_DIR}/../game_menu/menu_strings.cpp")
target_link_libraries(busy_notice_text_test PRIVATE torchlight_live)
target_compile_options(busy_notice_text_test PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(busy_notice_text_test PRIVATE
  TL_IMPORT_STRINGS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../../data/ui/tl_import_strings.txt")
add_test(NAME busy_notice_text_test COMMAND busy_notice_text_test)

add_executable(deferred_check_test deferred_check_test.cpp)
target_link_libraries(deferred_check_test PRIVATE torchlight_live)
target_compile_options(deferred_check_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME deferred_check_test COMMAND deferred_check_test)

# The consumer side (backend thread): links the frontend and the OGRE backend, so it is a library
# of its own; the queue and the store above stay testable without OGRE.
add_library(torchlight_live_mode STATIC live_content_source.cpp live_mode.cpp frame_timing.cpp
    frame_pacing.cpp frame_counter.cpp busy_notice.cpp frame_step.cpp install.cpp ui_overlay.cpp
    ui_gamepad.cpp named_texture_refresh.cpp
    system_back_button.cpp touch_input.cpp touch_overlay.cpp)
target_link_libraries(torchlight_live_mode PUBLIC torchlight_live torchlight_frontend torchlight_texture_files rex::runtime
    torchlight_platform torchlight_settings torchlight_input torchlight_adaptive_vsync)
target_include_directories(torchlight_live_mode PRIVATE "${TORCHLIGHT_REX_INCLUDE}")
target_compile_options(torchlight_live_mode PRIVATE -Wall -Wextra)
# The OGRE plugin directory; the backend objects themselves are linked by the executable.
target_compile_definitions(torchlight_live_mode PRIVATE
    $<TARGET_PROPERTY:torchlight_backend,INTERFACE_COMPILE_DEFINITIONS>)

add_executable(live_content_source_test live_content_source_test.cpp)
target_link_libraries(live_content_source_test PRIVATE torchlight_live_mode)
target_compile_options(live_content_source_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME live_content_source_test COMMAND live_content_source_test)

# The texture refresh queue and the named textures it matches.
add_executable(named_texture_refresh_test named_texture_refresh_test.cpp)
target_link_libraries(named_texture_refresh_test PRIVATE torchlight_live_mode)
target_compile_options(named_texture_refresh_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME named_texture_refresh_test COMMAND named_texture_refresh_test)

add_executable(ui_overlay_test ui_overlay_test.cpp)
target_link_libraries(ui_overlay_test PRIVATE torchlight_live_mode)
target_include_directories(ui_overlay_test PRIVATE "${TORCHLIGHT_REX_INCLUDE}")
target_compile_options(ui_overlay_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME ui_overlay_test COMMAND ui_overlay_test)

add_executable(ui_gamepad_test ui_gamepad_test.cpp)
target_link_libraries(ui_gamepad_test PRIVATE torchlight_live_mode)
target_include_directories(ui_gamepad_test PRIVATE "${TORCHLIGHT_REX_INCLUDE}")
target_compile_options(ui_gamepad_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME ui_gamepad_test COMMAND ui_gamepad_test)

add_executable(frame_counter_test frame_counter_test.cpp)
target_link_libraries(frame_counter_test PRIVATE torchlight_live_mode)
target_include_directories(frame_counter_test PRIVATE "${TORCHLIGHT_REX_INCLUDE}")
target_compile_options(frame_counter_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME frame_counter_test COMMAND frame_counter_test)

add_executable(frame_series_test frame_series_test.cpp)
target_link_libraries(frame_series_test PRIVATE torchlight_live_mode)
target_include_directories(frame_series_test PRIVATE "${TORCHLIGHT_REX_INCLUDE}")
target_compile_options(frame_series_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME frame_series_test COMMAND frame_series_test)

add_executable(frame_pacing_test frame_pacing_test.cpp frame_pacing.cpp)
target_include_directories(frame_pacing_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_options(frame_pacing_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME frame_pacing_test COMMAND frame_pacing_test)
