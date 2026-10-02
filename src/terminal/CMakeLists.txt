cmake_minimum_required(VERSION 3.14)

# This directory can also be added to an existing project without replacing its
# project name, enabling other languages, or building the complete xxfclib.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(xxfclib_terminal VERSION 0.1.0 LANGUAGES C)
endif()

get_filename_component(XXTERMINAL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${XXTERMINAL_ROOT}/xxterminal.cmake")
xxterminal_add_library(xxfclib_terminal)

option(XXTERMINAL_BUILD_TESTS "Build the focused terminal option tests" OFF)
if(XXTERMINAL_BUILD_TESTS)
    enable_testing()
    add_executable(test_terminal_options "${XXTERMINAL_ROOT}/tests/test_terminal_options.c")
    target_link_libraries(test_terminal_options PRIVATE xxfclib::terminal)
    add_test(NAME terminal_options COMMAND test_terminal_options)

    # Deterministically exercise unsupported SIMD handling without depending on
    # which CPU runs the tests. The test supplies the global feature functions.
    if(WIN32)
        set(_xxterminal_test_platform
            "${XXTERMINAL_ROOT}/src/terminal/platforms/xx_terminal_windows.c")
    else()
        set(_xxterminal_test_platform
            "${XXTERMINAL_ROOT}/src/terminal/platforms/xx_terminal_posix.c")
    endif()
    add_executable(test_terminal_options_unsupported
        "${XXTERMINAL_ROOT}/tests/test_terminal_options.c"
        "${XXTERMINAL_ROOT}/src/terminal/xx_terminal.c"
        "${_xxterminal_test_platform}"
    )
    target_include_directories(test_terminal_options_unsupported PRIVATE
        "${XXTERMINAL_INCLUDE_DIR}")
    target_compile_definitions(test_terminal_options_unsupported PRIVATE
        XXFC_STATIC XX_TERMINAL_OPTIONS_TEST_STUB_GLOBALS)
    if(WIN32)
        target_link_libraries(test_terminal_options_unsupported PRIVATE kernel32)
    endif()
    add_test(NAME terminal_options_unsupported COMMAND test_terminal_options_unsupported)

    foreach(_xxterminal_test test_terminal_options test_terminal_options_unsupported)
        target_compile_features(${_xxterminal_test} PRIVATE c_std_11)
        set_target_properties(${_xxterminal_test} PROPERTIES
            C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    endforeach()
    set_tests_properties(terminal_options terminal_options_unsupported
        PROPERTIES TIMEOUT 10)
endif()
