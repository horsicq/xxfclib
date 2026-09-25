/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_wiilz77_extractor.c - search raw data for WII LZ77.
 *
 * Scans for:
 *   4C 5A 37 37 10 at +0  ("LZ77.")
 *   4C 5A 37 37 11 at +0  ("LZ77.")
 *   49 4D 44 35 at +0  ("IMD5")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the wiilz77 reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/wiilz77/xx_wiilz77.h"

static const uint8_t k_anchor0[] = { 0x4C, 0x5A, 0x37, 0x37, 0x10 };
static const uint8_t k_anchor1[] = { 0x4C, 0x5A, 0x37, 0x37, 0x11 };
static const uint8_t k_anchor2[] = { 0x49, 0x4D, 0x44, 0x35 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
    { k_anchor1, sizeof(k_anchor1), 0U },
    { k_anchor2, sizeof(k_anchor2), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_WII_LZ77 };

static Abstractformat *xx_wiilz77_search_open(xx_io_device *window) {
    xx_wiilz77 *reader = xx_wiilz77_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_wiilz77_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_wiilz77_free((xx_wiilz77 *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_wiilz77_search_open, xx_wiilz77_search_close
};

static xx_format_search_state *xx_wiilz77_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_wiilz77_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_wiilz77_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_wiilz77_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_wiilz77_extractor = {
    xx_wiilz77_create_format_search,
    xx_wiilz77_get_current_format_info,
    xx_wiilz77_format_search_find_next,
    xx_wiilz77_free_format_search
};
