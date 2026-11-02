# The release's symbols push (push_symbols.sh), tested against a local repository. Bash and the
# compressors are POSIX tools here; the Windows release job runs it in Git's bash.
if(NOT WIN32)
    add_test(NAME push_symbols_test COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_push_symbols.sh)
endif()

# What release-check.yml shows of a game build in its public log (build_outcome.py).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME build_outcome_test
         COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test_build_outcome.py)
