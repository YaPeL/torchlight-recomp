# The release's symbols push (push_symbols.sh), tested against a local repository. Bash and the
# compressors are POSIX tools here; the Windows release job runs it in Git's bash.
if(NOT WIN32)
    add_test(NAME push_symbols_test COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_push_symbols.sh)
endif()
