/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_RESOURCEFORK_OUTPUT_NAME_H
#define XX_RESOURCEFORK_OUTPUT_NAME_H
#include "xxfclib/strings/xx_string.h"
#include <stdint.h>
#include <stdio.h>

/* Pascal names allow 255 bytes before Mac Roman expansion and the ID prefix.
 * Bound output components in UTF-8 bytes for portable filesystem limits.
 * The short form has no space after its four-digit ID, so it cannot collide
 * with a normal named resource. Its ordinal also separates duplicate IDs. */
static char *rsrc_output_name(const char *name, size_t ordinal)
{
    const char *p, *slash = NULL;
    size_t prefix, length;
    char *out;
    if (!name) return NULL;
    for (p = name; *p; ++p)
        if (*p == '/') slash = p;
    if (!slash || xx_str_len(slash + 1U) <= 240U) return xx_str_dup(name);
    if (ordinal > UINT32_MAX) return NULL;
    prefix = (size_t)(slash + 1U - name);
    for (size_t i = 0; i < 4U; ++i) {
        char c = slash[1U + i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return NULL;
    }
    length = prefix + 4U + 12U;
    out = xx_str_create_len(length);
    if (!out) return NULL;
    for (size_t i = 0; i < prefix + 4U; ++i) out[i] = name[i];
    snprintf(out + prefix + 4U, 13U, "~xx-%08x", (unsigned)ordinal);
    return out;
}
#endif
