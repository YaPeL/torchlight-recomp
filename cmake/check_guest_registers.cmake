# Fails if a file under SOURCE_DIR (src/) touches a guest register that the generated code may keep
# in a C++ local instead of PPCContext: the non-volatile r14-r31, f14-f31 and v14 and up
# (codegen non_volatile_as_local), and r12, which the game passes outside the ABI to SEH funclets
# and its stack probe (the reason non_argument_as_local stays off). A hook that reads its caller's
# preserved registers through ctx reads a stale value and fails silently; take the value from an
# argument of the enclosing call instead (docs/guest-hot-paths.md, "Codegen options"). Matches any
# access such as ctx.r28.u32, c.f30.f64 or ctx.v20.u8[...]. Run with cmake -DSOURCE_DIR=... -P.
cmake_minimum_required(VERSION 3.25)

# Justified exceptions, relative to SOURCE_DIR, each with its reason.
set(allowed
    # Guest-call doubles that scribble the caller's preserved registers on purpose, to prove the
    # achievement hooks do not read them.
    "achievements/guest_hooks_test.cpp"
)

set(register "(r(1[2-9]|2[0-9]|3[01])|f(1[4-9]|2[0-9]|3[01])|v(1[4-9]|[2-9][0-9]|1[01][0-9]|12[0-7]))")
set(access "[A-Za-z_][A-Za-z0-9_]*\\.${register}\\.(u|s|f)[0-9]+")

file(GLOB_RECURSE files LIST_DIRECTORIES false
     "${SOURCE_DIR}/*.cpp" "${SOURCE_DIR}/*.cc" "${SOURCE_DIR}/*.h" "${SOURCE_DIR}/*.hpp"
     "${SOURCE_DIR}/*.inl")
set(problems)
foreach(file IN LISTS files)
    file(RELATIVE_PATH relative "${SOURCE_DIR}" "${file}")
    if(relative IN_LIST allowed)
        continue()
    endif()
    file(READ "${file}" content)
    if(NOT content MATCHES "${access}")
        continue()
    endif()
    # One list item per line (semicolons and brackets would split or escape CMake list items).
    string(REPLACE ";" "<semicolon>" content "${content}")
    string(REPLACE "[" "<bracket>" content "${content}")
    string(REPLACE "]" "</bracket>" content "${content}")
    string(REPLACE "\n" ";" lines "${content}")
    set(number 0)
    foreach(line IN LISTS lines)
        math(EXPR number "${number} + 1")
        if(line MATCHES "${access}")
            string(STRIP "${line}" line)
            list(APPEND problems "  ${relative}:${number}: ${line}")
        endif()
    endforeach()
endforeach()
if(problems)
    list(JOIN problems "\n" text)
    string(REPLACE "<semicolon>" ";" text "${text}")
    string(REPLACE "<bracket>" "[" text "${text}")
    string(REPLACE "</bracket>" "]" text "${text}")
    message(FATAL_ERROR "guest registers the generated code may keep out of ctx (r12, r14-r31, "
                        "f14-f31, v14+); read an argument of the enclosing call instead:\n${text}")
endif()
message(STATUS "no access to r12 or preserved guest registers through ctx in ${SOURCE_DIR}")
