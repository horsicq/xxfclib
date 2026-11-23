# Available in both the full library and the formats profile.
option(XXFC_BUILD_APPLE_DIAGS_TESTS "Build native Apple Hardware Test LOC regression tests" OFF)
if(XXFC_BUILD_APPLE_DIAGS_TESTS)
    enable_testing()
    add_executable(test_apple_diags_native "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_apple_diags_native.c")
    target_link_libraries(test_apple_diags_native PRIVATE xxfclib::xxfclib)
    add_test(NAME xxfc_apple_diags_native COMMAND test_apple_diags_native)
endif()
