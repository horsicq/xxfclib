/* Copyright (c) 2024-2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * C port of XOptions::hexToGlobalColor/printConsole. Keep the upstream
 * palette ordering (nearest-color ties), ANSI codes and Windows attributes.
 */
#include "xxfclib/terminal/xx_terminal.h"

typedef struct terminal_color {
    uint32_t rgb;
    unsigned int ansi, native;
} terminal_color;

static const terminal_color palette[] = {
    {0x000000, 30, 0}, {0xffffff, 37, 7}, {0x808080, 90, 15}, {0xa0a0a4, 90, 8},
    {0xff0000, 31, 4}, {0x00ff00, 32, 2}, {0x0000ff, 34, 1}, {0x00ffff, 36, 3},
    {0xff00ff, 35, 5}, {0xffff00, 33, 6}, {0x800000, 91, 12}, {0x008000, 92, 10},
    {0x000080, 94, 9}, {0x008080, 96, 11}, {0x800080, 95, 13}, {0x808000, 93, 14}
};

static const terminal_color *nearest_color(uint32_t rgb)
{
    const terminal_color *best = &palette[0];
    unsigned int distance = UINT32_MAX;
    size_t i;
    if (rgb == UINT32_MAX) return NULL;
    for (i = 0; i < sizeof(palette) / sizeof(palette[0]); ++i) {
        int r = (int)((rgb >> 16) & 255) - (int)((palette[i].rgb >> 16) & 255);
        int g = (int)((rgb >> 8) & 255) - (int)((palette[i].rgb >> 8) & 255);
        int b = (int)(rgb & 255) - (int)(palette[i].rgb & 255);
        unsigned int d = (unsigned int)(r * r + g * g + b * b);
        if (d < distance) { distance = d; best = &palette[i]; }
    }
    return best;
}

static size_t append_code(char *text, size_t offset, unsigned int code)
{
    if (code >= 100) text[offset++] = '1';
    text[offset++] = (char)('0' + (code / 10) % 10);
    text[offset++] = (char)('0' + code % 10);
    return offset;
}

xxfc_status_t xx_terminal_write_color(const xx_terminal_state *state,
    const char *text, size_t size, uint32_t foreground, uint32_t background)
{
    const terminal_color *fg, *bg;
    xxfc_status_t status, restored;
    if (!state || (!text && size)) return XXFC_ERR_NULL_PARAM;
    if ((foreground != UINT32_MAX && foreground > 0xffffff) ||
        (background != UINT32_MAX && background > 0xffffff)) return XXFC_ERR_INVALID_ARG;
    if (!size || !state->valid || state->type == XX_TERMINAL_TYPE_NONE || !xx_is_color_output_enabled() ||
        (foreground == UINT32_MAX && background == UINT32_MAX))
        return xx_terminal_write(state, text, size);
    fg = nearest_color(foreground);
    bg = nearest_color(background);
    if (state->type == XX_TERMINAL_TYPE_ANSI) {
        char escape[12] = {'\033', '['};
        size_t length = append_code(escape, 2, fg ? fg->ansi : 39);
        escape[length++] = ';';
        length = append_code(escape, length, bg ? bg->ansi + 10 : 49);
        escape[length++] = 'm';
        status = xx_terminal_write(state, escape, length);
        if (status == XXFC_OK) status = xx_terminal_write(state, text, size);
        restored = xx_terminal_write(state, "\033[0m", 4);
        return status != XXFC_OK ? status : restored;
    }
    if (state->type == XX_TERMINAL_TYPE_WINDOWS) {
        uint16_t saved, attributes;
        if (!xx_terminal_get_attributes(state, &saved)) return xx_terminal_write(state, text, size);
        attributes = saved;
        if (fg) attributes = (uint16_t)((attributes & ~0x0fU) | fg->native);
        if (bg) attributes = (uint16_t)((attributes & ~0xf0U) | (bg->native << 4));
        if (!xx_terminal_set_attributes(state, attributes)) return xx_terminal_write(state, text, size);
        status = xx_terminal_write(state, text, size);
        restored = xx_terminal_set_attributes(state, saved) ? XXFC_OK : XXFC_ERR_IO;
        return status != XXFC_OK ? status : restored;
    }
    return xx_terminal_write(state, text, size);
}

static size_t append_number(char *text, size_t offset, unsigned int value)
{
    if (value >= 100) text[offset++] = (char)('0' + value / 100);
    if (value >= 10) text[offset++] = (char)('0' + (value / 10) % 10);
    text[offset++] = (char)('0' + value % 10);
    return offset;
}

/* The SGR parameters of one exact color: the palette's classic code for a
 * palette color, otherwise 38;2;r;g;b (48;2;r;g;b for a background). */
static size_t append_exact(char *text, size_t offset, uint32_t rgb, bool background)
{
    size_t i;
    for (i = 0; i < sizeof(palette) / sizeof(palette[0]); ++i)
        if (palette[i].rgb == rgb) return append_code(text, offset, palette[i].ansi + (background ? 10U : 0U));
    text[offset++] = background ? '4' : '3';
    text[offset++] = '8';
    text[offset++] = ';';
    text[offset++] = '2';
    text[offset++] = ';';
    offset = append_number(text, offset, (rgb >> 16) & 255);
    text[offset++] = ';';
    offset = append_number(text, offset, (rgb >> 8) & 255);
    text[offset++] = ';';
    return append_number(text, offset, rgb & 255);
}

xxfc_status_t xx_terminal_write_color_ex(const xx_terminal_state *state,
    const char *text, size_t size, uint32_t foreground, uint32_t background, uint32_t flags)
{
    static const char reset[] = "\033[0m";
    char span[512];
    size_t length = 2, i;
    xxfc_status_t status, restored;
    if (!(flags & XX_TERMINAL_COLOR_EXACT) || !state || state->type != XX_TERMINAL_TYPE_ANSI)
        return xx_terminal_write_color(state, text, size, foreground, background);
    if (!text && size) return XXFC_ERR_NULL_PARAM;
    if ((foreground != UINT32_MAX && foreground > 0xffffff) ||
        (background != UINT32_MAX && background > 0xffffff)) return XXFC_ERR_INVALID_ARG;
    if (!size || !state->valid || !xx_is_color_output_enabled() ||
        (foreground == UINT32_MAX && background == UINT32_MAX))
        return xx_terminal_write(state, text, size);
    /* "\033[" + two parameter lists of at most 16 characters + 'm' */
    span[0] = '\033';
    span[1] = '[';
    if (foreground != UINT32_MAX) length = append_exact(span, length, foreground, false);
    if (background != UINT32_MAX) {
        if (foreground != UINT32_MAX) span[length++] = ';';
        length = append_exact(span, length, background, true);
    }
    span[length++] = 'm';
    if (size <= sizeof(span) - length - (sizeof(reset) - 1)) {
        for (i = 0; i < size; ++i) span[length + i] = text[i];
        for (i = 0; i < sizeof(reset) - 1; ++i) span[length + size + i] = reset[i];
        return xx_terminal_write(state, span, length + size + sizeof(reset) - 1);
    }
    status = xx_terminal_write(state, span, length);
    if (status == XXFC_OK) status = xx_terminal_write(state, text, size);
    restored = xx_terminal_write(state, reset, sizeof(reset) - 1);
    return status != XXFC_OK ? status : restored;
}
