# The setup wizard (wizard.h): one window, played with a pad, a mouse or a keyboard. Desktop only:
# Android keeps its own first start. The SDK's SDL and Dear ImGui, with ImGui's two SDL backends,
# which the SDK does not install, and Noto Sans, built in: both fetched at fixed versions and checked
# by hash, nothing of theirs in the repository (THIRD_PARTY_NOTICES.md).
include(FetchContent)
FetchContent_Declare(wizard_imgui
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.5.tar.gz
  URL_HASH SHA256=0eb50fe9aeba1a51f96b5843c7f630a32ed2e9362d693c61b87e4fa870cf826d
  SOURCE_SUBDIR no-cmake-project
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(wizard_noto
  URL https://github.com/notofonts/latin-greek-cyrillic/releases/download/NotoSans-v2.015/NotoSans-v2.015.zip
  URL_HASH SHA256=0c34df072a3fa7efbb7cbf34950e1f971a4447cffe365d3a359e2d4089b958f5
  SOURCE_SUBDIR no-cmake-project
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(wizard_imgui wizard_noto)

set(embedded)
function(wizard_embed file name)
  set(out "${CMAKE_CURRENT_BINARY_DIR}/${name}.cpp")
  add_custom_command(OUTPUT "${out}"
    COMMAND ${CMAKE_COMMAND} "-DFILE=${file}" -DNAME=${name} "-DOUT=${out}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_blob.cmake"
    DEPENDS "${file}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_blob.cmake")
  set(embedded ${embedded} "${out}" PARENT_SCOPE)
endfunction()
wizard_embed("${wizard_noto_SOURCE_DIR}/NotoSans/unhinted/ttf/NotoSans-Regular.ttf" kFontRegular)
wizard_embed("${wizard_noto_SOURCE_DIR}/NotoSans/unhinted/ttf/NotoSans-Bold.ttf" kFontBold)
wizard_embed("${CMAKE_SOURCE_DIR}/data/icon/wizard-64.png" kWindowIcon)

# The listing has no window or runtime, tested on its own.
add_library(torchlight_folder_browser STATIC folder_browser.cpp)
target_include_directories(torchlight_folder_browser PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(torchlight_folder_browser PUBLIC cxx_std_23)
target_compile_options(torchlight_folder_browser PRIVATE -Wall -Wextra)
target_link_libraries(torchlight_folder_browser PUBLIC torchlight_user_folders)

add_executable(folder_browser_test folder_browser_test.cpp)
target_link_libraries(folder_browser_test PRIVATE torchlight_folder_browser)
target_compile_options(folder_browser_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME folder_browser_test COMMAND folder_browser_test)

add_library(torchlight_wizard STATIC wizard.cpp wizard_ui.cpp browser_page.cpp ${embedded}
            "${wizard_imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
            "${wizard_imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp")
# Their warnings are theirs.
set_source_files_properties(
  "${wizard_imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
  "${wizard_imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp"
  PROPERTIES COMPILE_OPTIONS "-w")
target_include_directories(torchlight_wizard PRIVATE "${wizard_imgui_SOURCE_DIR}/backends")
target_include_directories(torchlight_wizard PUBLIC "${TORCHLIGHT_REX_INCLUDE}")
target_link_libraries(torchlight_wizard PUBLIC torchlight_folder_browser torchlight_platform rex::runtime)
target_compile_options(torchlight_wizard PRIVATE -Wall -Wextra)
