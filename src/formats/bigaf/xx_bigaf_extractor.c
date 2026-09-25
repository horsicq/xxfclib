/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_bigaf_extractor.c - search raw data for BIGAF.
 *
 * Scans for:
 *   3C 62 69 67 61 66 3E 0A at +0  ("<bigaf>.")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the bigaf reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/bigaf/xx_bigaf.h"

static const uint8_t k_anchor0[] = { 0x3C, 0x62, 0x69, 0x67, 0x61, 0x66, 0x3E, 0x0A };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_BIGAF };

static Abstractformat *xx_bigaf_search_open(xx_io_device *window) {
    xx_bigaf *reader = xx_bigaf_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_bigaf_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_bigaf_free((xx_bigaf *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_bigaf_search_open, xx_bigaf_search_close
};

static xx_format_search_state *xx_bigaf_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_bigaf_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_bigaf_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_bigaf_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_bigaf_extractor = {
    xx_bigaf_create_format_search,
    xx_bigaf_get_current_format_info,
    xx_bigaf_format_search_find_next,
    xx_bigaf_free_format_search
};
