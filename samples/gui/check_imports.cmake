# Inspect normal and delayed imports after linking. No third-party PE parser needed.
execute_process(COMMAND "${DUMPBIN}" /imports "${EXECUTABLE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect GUI imports: ${error}")
endif()
string(REGEX MATCHALL "[A-Za-z0-9_.-]+\\.[dD][lL][lL]" imports "${output}")
list(REMOVE_DUPLICATES imports)
string(TOLOWER "${imports}" imports)
if(NOT imports STREQUAL "kernel32.dll")
    message(FATAL_ERROR "xxfc_gui must import only KERNEL32.dll; found: ${imports}")
endif()
message(STATUS "xxfc_gui imports verified: KERNEL32.dll only")
