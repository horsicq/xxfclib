/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_lpaq8_extractor.c - search raw data for LPAQ8.
 *
 * Scans for:
 *   70 51 08 30 at +0  ("pQ.0")
 *   70 51 08 31 at +0  ("pQ.1")
 *   70 51 08 32 at +0  ("pQ.2")
 *   70 51 08 33 at +0  ("pQ.3")
 *   70 51 08 34 at +0  ("pQ.4")
 *   70 51 08 35 at +0  ("pQ.5")
 *   70 51 08 36 at +0  ("pQ.6")
 *   70 51 08 37 at +0  ("pQ.7")
 *   70 51 08 38 at +0  ("pQ.8")
 *   70 51 08 39 at +0  ("pQ.9")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the lpaq8 reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/lpaq8/xx_lpaq8.h"

static const uint8_t k_anchor0[] = { 0x70, 0x51, 0x08, 0x30 };
static const uint8_t k_anchor1[] = { 0x70, 0x51, 0x08, 0x31 };
static const uint8_t k_anchor2[] = { 0x70, 0x51, 0x08, 0x32 };
static const uint8_t k_anchor3[] = { 0x70, 0x51, 0x08, 0x33 };
static const uint8_t k_anchor4[] = { 0x70, 0x51, 0x08, 0x34 };
static const uint8_t k_anchor5[] = { 0x70, 0x51, 0x08, 0x35 };
static const uint8_t k_anchor6[] = { 0x70, 0x51, 0x08, 0x36 };
static const uint8_t k_anchor7[] = { 0x70, 0x51, 0x08, 0x37 };
static const uint8_t k_anchor8[] = { 0x70, 0x51, 0x08, 0x38 };
static const uint8_t k_anchor9[] = { 0x70, 0x51, 0x08, 0x39 };

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
    { k_anchor9, sizeof(k_anchor9), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_LPAQ8 };

static Abstractformat *xx_lpaq8_search_open(xx_io_device *window) {
    xx_lpaq8 *reader = xx_lpaq8_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_lpaq8_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_lpaq8_free((xx_lpaq8 *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_lpaq8_search_open, xx_lpaq8_search_close
};

static xx_format_search_state *xx_lpaq8_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_lpaq8_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_lpaq8_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_lpaq8_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_lpaq8_extractor = {
    xx_lpaq8_create_format_search,
    xx_lpaq8_get_current_format_info,
    xx_lpaq8_format_search_find_next,
    xx_lpaq8_free_format_search
};
