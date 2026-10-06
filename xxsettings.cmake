# Settings plus the small runtime/global subset needed by standalone consumers.
# Define XXFC_STATIC, add XXSETTINGS_INCLUDE_DIR, and link XXSETTINGS_LIBRARIES.
set(XXSETTINGS_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/include")
set(XXSETTINGS_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/settings/xx_settings.c
    ${CMAKE_CURRENT_LIST_DIR}/src/settings/xx_settings_ini.c
    ${CMAKE_CURRENT_LIST_DIR}/src/settings/xx_settings_value.c
    ${CMAKE_CURRENT_LIST_DIR}/src/settings/xx_shortcuts.c
    ${CMAKE_CURRENT_LIST_DIR}/src/global/xx_settings_global.c
    ${CMAKE_CURRENT_LIST_DIR}/src/global/xx_global.c
    ${CMAKE_CURRENT_LIST_DIR}/src/global/platforms/xx_global_cpu.c
    ${CMAKE_CURRENT_LIST_DIR}/src/rt/xx_rt.c
    ${CMAKE_CURRENT_LIST_DIR}/src/rt/xx_rt_fp.c
    ${CMAKE_CURRENT_LIST_DIR}/src/rt/xx_rt_math.c
    ${CMAKE_CURRENT_LIST_DIR}/src/memory/xx_memory.c
    ${CMAKE_CURRENT_LIST_DIR}/src/memory/xx_memory_rt.c
    ${CMAKE_CURRENT_LIST_DIR}/src/memory/platforms/xx_memory_sse2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/memory/platforms/xx_memory_avx2.c
)
if(WIN32)
    list(APPEND XXSETTINGS_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/src/settings/platforms/xx_settings_file_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/settings/platforms/xx_settings_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/global/platforms/xx_global_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/rt/platforms/xx_rt_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/memory/platforms/xx_memory_windows.c
        ${CMAKE_CURRENT_LIST_DIR}/src/strings/platforms/xx_string_windows.c
    )
    set(XXSETTINGS_LIBRARIES kernel32)
else()
    list(APPEND XXSETTINGS_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/src/settings/platforms/xx_settings_file_posix.c
        ${CMAKE_CURRENT_LIST_DIR}/src/global/platforms/xx_global_posix.c
        ${CMAKE_CURRENT_LIST_DIR}/src/rt/platforms/xx_rt_posix.c
        ${CMAKE_CURRENT_LIST_DIR}/src/memory/platforms/xx_memory_posix.c
        ${CMAKE_CURRENT_LIST_DIR}/src/strings/platforms/xx_string_posix.c
    )
    find_package(Threads REQUIRED)
    set(XXSETTINGS_LIBRARIES Threads::Threads m)
    if(APPLE)
        list(APPEND XXSETTINGS_SOURCES ${CMAKE_CURRENT_LIST_DIR}/src/settings/platforms/xx_settings_macos.c)
        list(APPEND XXSETTINGS_LIBRARIES "-framework CoreFoundation")
    else()
        list(APPEND XXSETTINGS_SOURCES ${CMAKE_CURRENT_LIST_DIR}/src/settings/platforms/xx_settings_posix.c)
    endif()
endif()
