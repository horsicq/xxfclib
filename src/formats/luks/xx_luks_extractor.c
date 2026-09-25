/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_luks_extractor.c - search raw data for LUKS.
 *
 * Scans for:
 *   4C 55 4B 53 BA BE 00 01 at +0  ("LUKS....")
 *   4C 55 4B 53 BA BE 00 02 at +0  ("LUKS....")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the luks reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/luks/xx_luks.h"

static const uint8_t k_anchor0[] = { 0x4C, 0x55, 0x4B, 0x53, 0xBA, 0xBE, 0x00, 0x01 };
static const uint8_t k_anchor1[] = { 0x4C, 0x55, 0x4B, 0x53, 0xBA, 0xBE, 0x00, 0x02 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
    { k_anchor1, sizeof(k_anchor1), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_LUKS };

static Abstractformat *xx_luks_search_open(xx_io_device *window) {
    xx_luks *reader = xx_luks_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_luks_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_luks_free((xx_luks *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_luks_search_open, xx_luks_search_close
};

static xx_format_search_state *xx_luks_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_luks_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_luks_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_luks_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_luks_extractor = {
    xx_luks_create_format_search,
    xx_luks_get_current_format_info,
    xx_luks_format_search_find_next,
    xx_luks_free_format_search
};
