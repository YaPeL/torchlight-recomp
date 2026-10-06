# Fails if a file under SOURCE_DIR (src/) includes spdlog or calls it directly: spdlog::get,
# spdlog::info and the other level functions, the default logger. The project logs only through
# the SDK's REXLOG_* macros, which use the runtime's own loggers (rex::GetLoggerRaw). On Windows
# spdlog is a static library linked both into rexruntime.dll and into the executable, so spdlog's
# registry and default logger exist twice and the executable's copy is empty (docs/windows-port.md).
# spdlog::level:: (the level enum, plain values) is allowed. Run with cmake -DSOURCE_DIR=... -P.
cmake_minimum_required(VERSION 3.25)

file(GLOB_RECURSE files LIST_DIRECTORIES false
     "${SOURCE_DIR}/*.cpp" "${SOURCE_DIR}/*.cc" "${SOURCE_DIR}/*.h" "${SOURCE_DIR}/*.hpp"
     "${SOURCE_DIR}/*.inl")
set(problems)
foreach(file IN LISTS files)
    file(STRINGS "${file}" lines REGEX "spdlog")
    foreach(line IN LISTS lines)
        set(bad FALSE)
        if(line MATCHES "#[ \t]*include[ \t]*[<\"]spdlog")
            set(bad TRUE)
        else()
            # Every spdlog:: use that is not spdlog::level::.
            string(REGEX REPLACE "spdlog::level::" "" rest "${line}")
            if(rest MATCHES "spdlog::")
                set(bad TRUE)
            endif()
        endif()
        if(bad)
            file(RELATIVE_PATH relative "${SOURCE_DIR}" "${file}")
            string(STRIP "${line}" line)
            list(APPEND problems "  ${relative}: ${line}")
        endif()
    endforeach()
endforeach()
if(problems)
    list(JOIN problems "\n" text)
    message(FATAL_ERROR "spdlog used directly (log through REXLOG_* instead):\n${text}")
endif()
message(STATUS "no direct spdlog use in ${SOURCE_DIR}")
