INCLUDEPATH += $$PWD/include
DEPENDPATH += $$PWD/include
DEFINES += XXFC_STATIC
HEADERS += \
    $$PWD/include/xxfclib/global/xx_settings_global.h \
    $$PWD/include/xxfclib/settings/xx_settings.h \
    $$PWD/include/xxfclib/settings/xx_shortcuts.h \
    $$PWD/src/settings/xx_settings_internal.h \
    $$PWD/src/settings/platforms/xx_settings_platform.h
SOURCES += \
    $$PWD/src/settings/xx_settings.c \
    $$PWD/src/settings/xx_settings_ini.c \
    $$PWD/src/settings/xx_settings_value.c \
    $$PWD/src/settings/xx_shortcuts.c \
    $$PWD/src/global/xx_settings_global.c \
    $$PWD/src/global/xx_global.c \
    $$PWD/src/global/platforms/xx_global_cpu.c \
    $$PWD/src/rt/xx_rt.c \
    $$PWD/src/rt/xx_rt_fp.c \
    $$PWD/src/rt/xx_rt_math.c \
    $$PWD/src/memory/xx_memory.c \
    $$PWD/src/memory/xx_memory_rt.c \
    $$PWD/src/memory/platforms/xx_memory_sse2.c \
    $$PWD/src/memory/platforms/xx_memory_avx2.c
win32 {
    SOURCES += \
        $$PWD/src/settings/platforms/xx_settings_file_windows.c \
        $$PWD/src/settings/platforms/xx_settings_windows.c \
        $$PWD/src/global/platforms/xx_global_windows.c \
        $$PWD/src/rt/platforms/xx_rt_windows.c \
        $$PWD/src/memory/platforms/xx_memory_windows.c \
        $$PWD/src/strings/platforms/xx_string_windows.c
} else {
    SOURCES += \
        $$PWD/src/settings/platforms/xx_settings_file_posix.c \
        $$PWD/src/global/platforms/xx_global_posix.c \
        $$PWD/src/rt/platforms/xx_rt_posix.c \
        $$PWD/src/memory/platforms/xx_memory_posix.c \
        $$PWD/src/strings/platforms/xx_string_posix.c
    LIBS += -lm -lpthread
    macx {
        SOURCES += $$PWD/src/settings/platforms/xx_settings_macos.c
        LIBS += -framework CoreFoundation
    } else {
        SOURCES += $$PWD/src/settings/platforms/xx_settings_posix.c
    }
}
SOURCES = $$unique(SOURCES)
