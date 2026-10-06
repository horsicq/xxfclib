# Minimal terminal/global sources for consumers that do not link the full library.
# Existing consumers may use XXTERMINAL_SOURCES/XXTERMINAL_INCLUDE_DIR directly,
# or call xxterminal_add_library(target) to create a configured static library.
set(XXTERMINAL_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/include")
set(XXTERMINAL_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/terminal/xx_terminal.c
    ${CMAKE_CURRENT_LIST_DIR}/src/global/xx_global.c
    ${CMAKE_CURRENT_LIST_DIR}/src/global/platforms/xx_global_cpu.c
)
if(WIN32)
    list(APPEND XXTERMINAL_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/src/terminal/platforms/xx_terminal_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/global/platforms/xx_global_windows.c
    )
else()
    list(APPEND XXTERMINAL_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/src/terminal/platforms/xx_terminal_posix.c
        ${CMAKE_CURRENT_LIST_DIR}/src/global/platforms/xx_global_posix.c
    )
endif()

function(xxterminal_add_library target)
    add_library(${target} STATIC ${XXTERMINAL_SOURCES})
    target_include_directories(${target} PUBLIC "${XXTERMINAL_INCLUDE_DIR}")
    target_compile_definitions(${target} PUBLIC XXFC_STATIC)
    target_compile_features(${target} PRIVATE c_std_11)
    set_target_properties(${target} PROPERTIES
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    if(WIN32)
        target_link_libraries(${target} PUBLIC kernel32)
    endif()
    if(target STREQUAL "xxfclib_terminal" AND NOT TARGET xxfclib::terminal)
        add_library(xxfclib::terminal ALIAS ${target})
    endif()
endfunction()
