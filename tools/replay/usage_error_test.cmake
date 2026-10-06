# replay with a bad command line must exit 2 with the usage on stderr (it used to take any unknown
# argument for the capture and abort creating its folder).
foreach(args "--no_such_option" "" "a.tlcap;b.tlcap" "--session;x.tlses")
  execute_process(COMMAND ${REPLAY} ${args} RESULT_VARIABLE result ERROR_VARIABLE err
                  OUTPUT_QUIET)
  if(NOT result EQUAL 2 OR NOT err MATCHES "Usage: replay")
    message(FATAL_ERROR "replay ${args}: exit ${result}, stderr: ${err}")
  endif()
endforeach()
