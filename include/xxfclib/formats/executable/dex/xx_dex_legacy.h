/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* Values of YARA 3.11's legacy dex module.
 *
 * Rule sets written for yara-python-dex (YARA 3.11's dex module, APKiD's for
 * example) use parts of that module that YARA-X's dex module (xx_dex_yx.h)
 * does not have: the fields and methods defined in class data and the
 * string ID offsets. xx_dex_legacy_analyze() walks a DEX the way YARA 3.11's
 * dex_parse() does, quirks included, and computes the values such rules
 * test, so a scanner can hand them to the rules as globals. Every read is
 * bounds checked; the input is never modified.
 */

#ifndef XXFCLIB_DEX_LEGACY_H
#define XXFCLIB_DEX_LEGACY_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_dex_legacy {
    /* YARA 3.11 parses the data: magic "dex\n035\0" to "dex\n038\0" and a
       whole header. Otherwise every dex.* value is undefined there. */
    bool parsed;
    /* for i in (0..dex.header.field_ids_size): dex.field[i].name has a
       byte outside 0x00-0x7F (what /[^\x00-\x7F]{1,4}/ finds) */
    int64_t unicode_field_names;
    /* the same over dex.method[i], i <= dex.header.method_ids_size */
    int64_t unicode_method_names;
    /* for any i in (0..dex.header.string_ids_size - 1) :
       (dex.string_ids[i + 1].offset < dex.string_ids[i].offset) */
    bool unsorted_string_pool;
} xx_dex_legacy;

/** Fill *result for the DEX in data. Returns false only when out of memory
 * (the values are then incomplete). */
XXFC_API bool xx_dex_legacy_analyze(const uint8_t *data, size_t size, xx_dex_legacy *result);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_DEX_LEGACY_H */
