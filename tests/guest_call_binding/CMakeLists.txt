# tools/android/check_guest_call_binding.py on miniature builds, so the check itself is exercised
# where the game is not built (the no-game CI jobs). On Android the fixtures are shared libraries,
# as the game is there: the check has to pass the library linked with the option patch 47 gives
# the game and fail the other two. The option is added here directly: this tests the check, not
# the SDK's helper (a game link without it fails the check after the link). On the desktop they are
# executables, the platform's own format (ELF, Mach-O, or PE with the linker's map): the check has
# to pass the hooked one and fail the one with a bypass.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(ANDROID)
    add_library(guest_call_binding_ok SHARED fixture.cpp hook.cpp)
    target_link_options(guest_call_binding_ok PRIVATE -Wl,-Bsymbolic-functions)
    add_library(guest_call_binding_plt SHARED fixture.cpp hook.cpp)
    add_library(guest_call_binding_bypass SHARED fixture.cpp hook.cpp)
    target_link_options(guest_call_binding_bypass PRIVATE -Wl,-Bsymbolic-functions)
    set(_fixtures guest_call_binding_ok guest_call_binding_plt guest_call_binding_bypass)
    set(_arguments $<TARGET_FILE:guest_call_binding_ok> $<TARGET_FILE:guest_call_binding_bypass>
        --plt $<TARGET_FILE:guest_call_binding_plt>)
else()
    add_executable(guest_call_binding_ok fixture.cpp hook.cpp main.cpp)
    add_executable(guest_call_binding_bypass fixture.cpp hook.cpp main.cpp)
    set(_fixtures guest_call_binding_ok guest_call_binding_bypass)
    set(_arguments $<TARGET_FILE:guest_call_binding_ok> $<TARGET_FILE:guest_call_binding_bypass>)
    if(WIN32)
        foreach(_fixture IN ITEMS ok bypass)
            set(_map "${CMAKE_CURRENT_BINARY_DIR}/guest_call_binding_${_fixture}.map")
            target_link_options(guest_call_binding_${_fixture} PRIVATE "LINKER:/MAP:${_map}"
                                "LINKER:/INCREMENTAL:NO")
            list(APPEND _arguments --${_fixture}-map "${_map}")
        endforeach()
    endif()
endif()
target_compile_definitions(guest_call_binding_bypass PRIVATE FIXTURE_BYPASS)

set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/guest_call_binding.stamp")
add_custom_command(OUTPUT "${_stamp}"
    COMMAND ${Python3_EXECUTABLE} -I
            ${CMAKE_SOURCE_DIR}/tools/android/test_check_guest_call_binding.py
            ${_arguments} ${_binding_tools}
    COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
    DEPENDS ${_fixtures}
            ${CMAKE_SOURCE_DIR}/tools/android/check_guest_call_binding.py
            ${CMAKE_SOURCE_DIR}/tools/android/test_check_guest_call_binding.py
    COMMENT "Checking check_guest_call_binding.py on its fixtures"
    VERBATIM)
add_custom_target(guest_call_binding_check ALL DEPENDS "${_stamp}")
