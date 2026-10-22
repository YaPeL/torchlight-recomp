# tools/android/check_guest_call_binding.py on miniature libraries, so the check itself is
# exercised where the game is not built (the Android CI job): in the default build, the check
# has to pass the library linked with the option patch 47 gives the game and fail the other two.
# The option is added here directly: this tests the check, not the SDK's helper (a game link
# without it fails the check after the link).
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_library(guest_call_binding_ok SHARED fixture.cpp hook.cpp)
target_link_options(guest_call_binding_ok PRIVATE -Wl,-Bsymbolic-functions)
add_library(guest_call_binding_plt SHARED fixture.cpp hook.cpp)
add_library(guest_call_binding_bypass SHARED fixture.cpp hook.cpp)
target_compile_definitions(guest_call_binding_bypass PRIVATE FIXTURE_BYPASS)
target_link_options(guest_call_binding_bypass PRIVATE -Wl,-Bsymbolic-functions)

set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/guest_call_binding.stamp")
add_custom_command(OUTPUT "${_stamp}"
    COMMAND ${Python3_EXECUTABLE} -I
            ${CMAKE_SOURCE_DIR}/tools/android/test_check_guest_call_binding.py
            $<TARGET_FILE:guest_call_binding_ok> $<TARGET_FILE:guest_call_binding_plt>
            $<TARGET_FILE:guest_call_binding_bypass>
            --nm ${CMAKE_NM} --objdump ${CMAKE_OBJDUMP}
    COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
    DEPENDS guest_call_binding_ok guest_call_binding_plt guest_call_binding_bypass
            ${CMAKE_SOURCE_DIR}/tools/android/check_guest_call_binding.py
            ${CMAKE_SOURCE_DIR}/tools/android/test_check_guest_call_binding.py
    COMMENT "Checking check_guest_call_binding.py on its fixtures"
    VERBATIM)
add_custom_target(guest_call_binding_check ALL DEPENDS "${_stamp}")
