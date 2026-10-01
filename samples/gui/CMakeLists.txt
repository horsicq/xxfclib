# Enforce the promised kernel32-only import table rather than silently using a CRT.
if(NOT WIN32 OR NOT XXFC_MSVC_X64 OR NOT XXFC_STATIC_NO_CRT)
    message(FATAL_ERROR "xxfc_gui requires MSVC x64 and XXFC_STATIC_NO_CRT=ON")
endif()
if(XXFC_BUILD_BOTH)
    set(_gui_library xxfclib_static)
elseif(BUILD_SHARED_LIBS)
    message(FATAL_ERROR "xxfc_gui requires a static xxfclib (BUILD_SHARED_LIBS=OFF or XXFC_BUILD_BOTH=ON)")
else()
    set(_gui_library xxfclib)
endif()
add_executable(xxfc_gui WIN32 main.c ../unpack/xxfc_readers.c ../../src/platforms/xx_entry_windows.c)
target_include_directories(xxfc_gui PRIVATE ../unpack)
target_link_libraries(xxfc_gui PRIVATE ${_gui_library} kernel32.lib)
target_compile_definitions(xxfc_gui PRIVATE XXFC_NO_CRT)
target_compile_options(xxfc_gui PRIVATE /W4 /utf-8 /GS- /GL- /Zl)
set_target_properties(xxfc_gui PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded" MSVC_RUNTIME_CHECKS "")
target_link_options(xxfc_gui PRIVATE /NODEFAULTLIB /ENTRY:xxfc_gui_entry /OPT:REF /OPT:ICF)

get_filename_component(_gui_tools "${CMAKE_LINKER}" DIRECTORY)
find_program(XXFC_GUI_DUMPBIN dumpbin HINTS "${_gui_tools}")
if(NOT XXFC_GUI_DUMPBIN)
    message(FATAL_ERROR "dumpbin is required to verify xxfc_gui imports")
endif()
add_custom_command(TARGET xxfc_gui POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        "-DDUMPBIN=${XXFC_GUI_DUMPBIN}"
        "-DEXECUTABLE=$<TARGET_FILE:xxfc_gui>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_imports.cmake"
    VERBATIM)

option(XXFC_GUI_BUILD_SMOKE_TESTS "Build a hidden-window GUI integration test" OFF)
if(XXFC_GUI_BUILD_SMOKE_TESTS)
    add_executable(test_xxfc_gui_smoke
        ../../tests/test_gui_smoke.c ../unpack/xxfc_readers.c
        ../../src/platforms/xx_entry_windows.c)
    target_include_directories(test_xxfc_gui_smoke PRIVATE ../unpack)
    target_link_libraries(test_xxfc_gui_smoke PRIVATE ${_gui_library} kernel32.lib)
    target_compile_definitions(test_xxfc_gui_smoke PRIVATE XXFC_NO_CRT)
    target_compile_options(test_xxfc_gui_smoke PRIVATE /W4 /utf-8 /GS- /GL- /Zl)
    set_target_properties(test_xxfc_gui_smoke PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded" MSVC_RUNTIME_CHECKS "")
    target_link_options(test_xxfc_gui_smoke PRIVATE
        /NODEFAULTLIB /ENTRY:xxfc_gui_smoke_entry /OPT:REF /OPT:ICF)
endif()

if(XXFC_GUI_BUILD_SMOKE_TESTS)
    add_executable(test_xxfc_unpack_smoke
        ../../tests/test_unpack_smoke.c ../unpack/xxfc_readers.c
        ../../src/platforms/xx_entry_windows.c)
    target_include_directories(test_xxfc_unpack_smoke PRIVATE ../unpack)
    target_link_libraries(test_xxfc_unpack_smoke PRIVATE ${_gui_library} kernel32.lib)
    target_compile_definitions(test_xxfc_unpack_smoke PRIVATE XXFC_NO_CRT)
    target_compile_options(test_xxfc_unpack_smoke PRIVATE /W4 /utf-8 /GS- /GL- /Zl)
    set_target_properties(test_xxfc_unpack_smoke PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded" MSVC_RUNTIME_CHECKS "")
    target_link_options(test_xxfc_unpack_smoke PRIVATE
        /NODEFAULTLIB /ENTRY:xxfc_unpack_smoke_entry /OPT:REF /OPT:ICF)
endif()
