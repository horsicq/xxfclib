/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#if defined(_WIN32)

#include "xx_terminal_platform.h"
#include <windows.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

static HANDLE xx_terminal_handle(const xx_terminal_state *state) {
    return GetStdHandle(state->stream == XX_TERMINAL_STDERR ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
}

void xx_terminal_platform_init(xx_terminal_state *state, xx_terminal_type_t type) {
    HANDLE handle = xx_terminal_handle(state);
    DWORD mode = 0;
    CONSOLE_SCREEN_BUFFER_INFO info;

    if (!handle || handle == INVALID_HANDLE_VALUE || !GetConsoleMode(handle, &mode)) {
        return;
    }
    state->valid = true;
    state->original_mode = mode;
    state->current_mode = mode;
    if (GetConsoleScreenBufferInfo(handle, &info)) {
        state->attributes_valid = true;
        state->original_attributes = info.wAttributes;
    }
    if (type == XX_TERMINAL_TYPE_NONE) {
        return;
    }
    state->type = XX_TERMINAL_TYPE_WINDOWS;
#ifndef _USING_V110_SDK71_
    if (type == XX_TERMINAL_TYPE_ANSI &&
        SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        state->type = XX_TERMINAL_TYPE_ANSI;
        state->current_mode = mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    }
#endif
}

void xx_terminal_platform_finish(const xx_terminal_state *state) {
    HANDLE handle = xx_terminal_handle(state);
    if (state->valid) {
        if (state->attributes_valid) {
            SetConsoleTextAttribute(handle, state->original_attributes);
        }
        if (state->current_mode != state->original_mode) {
            SetConsoleMode(handle, state->original_mode);
        }
    }
}

xxfc_status_t xx_terminal_platform_write(const xx_terminal_state *state,
                                         const char *text, size_t size) {
    HANDLE handle = xx_terminal_handle(state);
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        return XXFC_ERR_IO;
    }
    while (size) {
        DWORD chunk = size > 0x7fffffffU ? 0x7fffffffU : (DWORD)size;
        DWORD written = 0;
        if (!WriteFile(handle, text, chunk, &written, NULL) || !written) {
            return XXFC_ERR_IO;
        }
        text += written;
        size -= written;
    }
    return XXFC_OK;
}

xxfc_status_t xx_terminal_platform_flush(const xx_terminal_state *state) {
    HANDLE handle = xx_terminal_handle(state);
    /* WriteFile is unbuffered here; FlushFileBuffers is invalid on consoles. */
    return handle && handle != INVALID_HANDLE_VALUE ? XXFC_OK : XXFC_ERR_IO;
}

bool xx_terminal_platform_get_attributes(const xx_terminal_state *state, uint16_t *attributes) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(xx_terminal_handle(state), &info)) {
        return false;
    }
    *attributes = info.wAttributes;
    return true;
}

bool xx_terminal_platform_set_attributes(const xx_terminal_state *state, uint16_t attributes) {
    return SetConsoleTextAttribute(xx_terminal_handle(state), attributes) != 0;
}

#endif
