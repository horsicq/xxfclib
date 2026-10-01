/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_TERMINAL_PLATFORM_H
#define XX_TERMINAL_PLATFORM_H

#include "xxfclib/terminal/xx_terminal.h"

void xx_terminal_platform_init(xx_terminal_state *state, xx_terminal_type_t type);
void xx_terminal_platform_finish(const xx_terminal_state *state);
xxfc_status_t xx_terminal_platform_write(const xx_terminal_state *state,
                                         const char *text, size_t size);
xxfc_status_t xx_terminal_platform_flush(const xx_terminal_state *state);
bool xx_terminal_platform_get_attributes(const xx_terminal_state *state,
                                          uint16_t *attributes);
bool xx_terminal_platform_set_attributes(const xx_terminal_state *state,
                                          uint16_t attributes);

#endif
