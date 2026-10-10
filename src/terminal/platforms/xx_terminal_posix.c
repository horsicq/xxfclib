/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#if !defined(_WIN32)

#include "xx_terminal_platform.h"
#include <stdio.h>
#include <unistd.h>

static FILE *xx_terminal_file(const xx_terminal_state *state)
{
    return state->stream == XX_TERMINAL_STDERR ? stderr : stdout;
}

void xx_terminal_platform_init(xx_terminal_state *state, xx_terminal_type_t type)
{
    state->valid = isatty(state->stream == XX_TERMINAL_STDERR ? STDERR_FILENO : STDOUT_FILENO) != 0;
    if (state->valid && type == XX_TERMINAL_TYPE_ANSI) {
        state->type = type;
    }
}

void xx_terminal_platform_finish(const xx_terminal_state *state)
{
    (void)state;
}

xxfc_status_t xx_terminal_platform_write(const xx_terminal_state *state, const char *text, size_t size)
{
    return fwrite(text, 1, size, xx_terminal_file(state)) == size ? XXFC_OK : XXFC_ERR_IO;
}

xxfc_status_t xx_terminal_platform_flush(const xx_terminal_state *state)
{
    return fflush(xx_terminal_file(state)) == 0 ? XXFC_OK : XXFC_ERR_IO;
}

bool xx_terminal_platform_get_attributes(const xx_terminal_state *state, uint16_t *attributes)
{
    (void)state;
    (void)attributes;
    return false;
}

bool xx_terminal_platform_set_attributes(const xx_terminal_state *state, uint16_t attributes)
{
    (void)state;
    (void)attributes;
    return false;
}

#endif
