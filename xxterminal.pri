INCLUDEPATH += $$PWD/include
DEPENDPATH += $$PWD/include
DEFINES += XXFC_STATIC

HEADERS += \
    $$PWD/include/xxfclib/terminal/xx_terminal.h \
    $$PWD/include/xxfclib/global/xx_global.h \
    $$PWD/src/terminal/platforms/xx_terminal_platform.h \
    $$PWD/src/global/platforms/xx_global_platform.h

SOURCES += \
    $$PWD/src/terminal/xx_terminal.c \
    $$PWD/src/global/xx_global.c \
    $$PWD/src/global/platforms/xx_global_cpu.c

win32 {
    SOURCES += \
        $$PWD/src/terminal/platforms/xx_terminal_windows.c \
        $$PWD/src/global/platforms/xx_global_windows.c
} else {
    SOURCES += \
        $$PWD/src/terminal/platforms/xx_terminal_posix.c \
        $$PWD/src/global/platforms/xx_global_posix.c
}
