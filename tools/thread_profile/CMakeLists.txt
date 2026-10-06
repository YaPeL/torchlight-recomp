# CPU per thread and where the busiest threads spend it, measured on a running game from outside
# (Windows; on Linux tools/profile_utilization.py and tools/profile_guest.py).
if(WIN32)
    add_executable(thread_cpu thread_cpu.cpp)
    target_compile_options(thread_cpu PRIVATE -Wall -Wextra)
    add_executable(thread_sample thread_sample.cpp)
    target_link_libraries(thread_sample PRIVATE dbghelp)
    target_compile_options(thread_sample PRIVATE -Wall -Wextra)
endif()
