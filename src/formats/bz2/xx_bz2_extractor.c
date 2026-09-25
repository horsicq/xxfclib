/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_bz2_extractor.c - search raw data for BZ2.
 *
 * Scans for:
 *   42 5A 68 31 at +0  ("BZh1")
 *   42 5A 68 32 at +0  ("BZh2")
 *   42 5A 68 33 at +0  ("BZh3")
 *   42 5A 68 34 at +0  ("BZh4")
 *   42 5A 68 35 at +0  ("BZh5")
 *   42 5A 68 36 at +0  ("BZh6")
 *   42 5A 68 37 at +0  ("BZh7")
 *   42 5A 68 38 at +0  ("BZh8")
 *   42 5A 68 39 at +0  ("BZh9")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the bz2 reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/bz2/xx_bz2.h"

static const uint8_t k_anchor0[] = { 0x42, 0x5A, 0x68, 0x31 };
static const uint8_t k_anchor1[] = { 0x42, 0x5A, 0x68, 0x32 };
static const uint8_t k_anchor2[] = { 0x42, 0x5A, 0x68, 0x33 };
static const uint8_t k_anchor3[] = { 0x42, 0x5A, 0x68, 0x34 };
static const uint8_t k_anchor4[] = { 0x42, 0x5A, 0x68, 0x35 };
static const uint8_t k_anchor5[] = { 0x42, 0x5A, 0x68, 0x36 };
static const uint8_t k_anchor6[] = { 0x42, 0x5A, 0x68, 0x37 };
static const uint8_t k_anchor7[] = { 0x42, 0x5A, 0x68, 0x38 };
static const uint8_t k_anchor8[] = { 0x42, 0x5A, 0x68, 0x39 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
    { k_anchor1, sizeof(k_anchor1), 0U },
    { k_anchor2, sizeof(k_anchor2), 0U },
    { k_anchor3, sizeof(k_anchor3), 0U },
    { k_anchor4, sizeof(k_anchor4), 0U },
    { k_anchor5, sizeof(k_anchor5), 0U },
    { k_anchor6, sizeof(k_anchor6), 0U },
    { k_anchor7, sizeof(k_anchor7), 0U },
    { k_anchor8, sizeof(k_anchor8), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_BZ2 };

static Abstractformat *xx_bz2_search_open(xx_io_device *window) {
    xx_bz2 *reader = xx_bz2_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_bz2_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_bz2_free((xx_bz2 *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_bz2_search_open, xx_bz2_search_close
};

static xx_format_search_state *xx_bz2_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_bz2_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_bz2_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_bz2_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_bz2_extractor = {
    xx_bz2_create_format_search,
    xx_bz2_get_current_format_info,
    xx_bz2_format_search_find_next,
    xx_bz2_free_format_search
};
