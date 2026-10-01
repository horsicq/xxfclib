/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/terminal/xx_terminal.h"
#include "platforms/xx_terminal_platform.h"
#include "../global/platforms/xx_global_platform.h"

static bool xx_terminal_valid_stream(xx_terminal_stream_t stream) {
    return stream == XX_TERMINAL_STDOUT || stream == XX_TERMINAL_STDERR;
}

xx_terminal_state xx_terminal_init(xx_terminal_stream_t stream) {
    xx_terminal_state state = {0};
    xx_terminal_type_t type;

    state.stream = stream;
    if (!xx_terminal_valid_stream(stream)) {
        return state;
    }
    type = xx_get_terminal_type();
    if (stream == XX_TERMINAL_STDERR) {
        type = xx_global_platform_detect_terminal_type(true);
    }
    xx_terminal_platform_init(&state, type);
    return state;
}

void xx_terminal_finish(const xx_terminal_state *state) {
    if (state && xx_terminal_valid_stream(state->stream)) {
        xx_terminal_platform_finish(state);
    }
}

xxfc_status_t xx_terminal_write(const xx_terminal_state *state,
                                const char *text, size_t size) {
    if (!state || (!text && size)) {
        return XXFC_ERR_NULL_PARAM;
    }
    if (!xx_terminal_valid_stream(state->stream)) {
        return XXFC_ERR_INVALID_ARG;
    }
    if (!size) {
        return XXFC_OK;
    }
    return xx_terminal_platform_write(state, text, size);
}

xxfc_status_t xx_terminal_print(const xx_terminal_state *state, const char *text) {
    size_t size = 0;
    if (!text) {
        return XXFC_ERR_NULL_PARAM;
    }
    while (text[size]) {
        ++size;
    }
    return xx_terminal_write(state, text, size);
}

xxfc_status_t xx_terminal_flush(const xx_terminal_state *state) {
    if (!state) {
        return XXFC_ERR_NULL_PARAM;
    }
    if (!xx_terminal_valid_stream(state->stream)) {
        return XXFC_ERR_INVALID_ARG;
    }
    return xx_terminal_platform_flush(state);
}

bool xx_terminal_get_attributes(const xx_terminal_state *state, uint16_t *attributes) {
    return state && attributes && xx_terminal_valid_stream(state->stream) &&
           xx_terminal_platform_get_attributes(state, attributes);
}

bool xx_terminal_set_attributes(const xx_terminal_state *state, uint16_t attributes) {
    return state && xx_terminal_valid_stream(state->stream) &&
           xx_terminal_platform_set_attributes(state, attributes);
}
