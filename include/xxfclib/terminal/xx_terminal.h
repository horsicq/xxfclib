/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_TERMINAL_H
#define XX_TERMINAL_H

#include "xxfclib/global/xx_global.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xx_terminal_stream_e {
    XX_TERMINAL_STDOUT = 0,
    XX_TERMINAL_STDERR
} xx_terminal_stream_t;

typedef struct xx_terminal_state_s {
    xx_terminal_stream_t stream;
    xx_terminal_type_t type;
    uint32_t original_mode;
    uint32_t current_mode;
    uint16_t original_attributes;
    bool valid;
    bool attributes_valid;
} xx_terminal_state;

/**
 * @brief Apply one common command-line option to the process-global settings.
 * Recognizes --sse2/--nosse2, --avx2/--noavx2 and --color/--nocolor.
 * The --no-sse2, --no-avx2 and --no-color spellings are also accepted.
 * CPU options are recognized but ignored if the CPU/OS lacks the feature.
 * Supported CPU features and color output are enabled by default; options do
 * not reset existing settings, so the last option for each setting wins.
 * Apply options before starting work. The caller handles --, application
 * options and positional arguments. This function performs no allocation.
 * @return true for a recognized option, false for NULL or an unknown option.
 */
XXFC_API bool xx_terminal_handle_option(const char *option);

/**
 * @brief Prepare a standard stream for terminal output.
 * Stdout uses the global terminal type; stderr is detected independently.
 * Redirected streams remain plain. Pair each init with finish, in reverse order.
 */
XXFC_API xx_terminal_state xx_terminal_init(xx_terminal_stream_t stream);
/** @brief Restore the console mode and attributes saved by init. */
XXFC_API void xx_terminal_finish(const xx_terminal_state *state);

/** @brief Write exactly size bytes, including embedded NULs. No formatting. */
XXFC_API xxfc_status_t xx_terminal_write(const xx_terminal_state *state, const char *text, size_t size);
/** @brief Write a NUL-terminated string without interpreting format specifiers. */
XXFC_API xxfc_status_t xx_terminal_print(const xx_terminal_state *state, const char *text);
XXFC_API xxfc_status_t xx_terminal_flush(const xx_terminal_state *state);

/** @brief Read/write raw native Windows attributes. Color mapping is the caller's responsibility. */
XXFC_API bool xx_terminal_get_attributes(const xx_terminal_state *state, uint16_t *attributes);
XXFC_API bool xx_terminal_set_attributes(const xx_terminal_state *state, uint16_t attributes);

#ifdef __cplusplus
}
#endif

#endif /* XX_TERMINAL_H */
