/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SHORTCUTS_H
#define XX_SHORTCUTS_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_shortcut_s {
    const char *action;   /* Named application action, UTF-8. */
    const char *sequence; /* e.g. Ctrl+O, Ctrl+Shift+E, F5; empty disables. */
} xx_shortcut;
typedef struct xx_shortcuts_s xx_shortcuts;

/* Load [shortcuts] action=sequence entries, overriding copied defaults by name.
 * NULL path means "shortcuts.ini". Missing files retain defaults; unknown
 * actions are appended. Does not write the file. Key syntax is validated by
 * the consumer. On failure *out is NULL. Strings/lists belong to the result. */
XXFC_API xxfc_status_t xx_shortcuts_load(const char *utf8_path,
    const xx_shortcut *defaults, size_t count, xx_shortcuts **out);
XXFC_API void xx_shortcuts_destroy(xx_shortcuts *shortcuts);
XXFC_API size_t xx_shortcuts_count(const xx_shortcuts *shortcuts);
XXFC_API const xx_shortcut *xx_shortcuts_at(const xx_shortcuts *shortcuts, size_t index);

#ifdef __cplusplus
}
#endif
#endif
