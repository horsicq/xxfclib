/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_shrs_extractor.c - search raw data for D-Link SHRS.
 *
 * Scans for:
 *   53 48 52 53 at +0  ("SHRS")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the shrs reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/shrs/xx_shrs.h"

static const uint8_t k_anchor0[] = { 0x53, 0x48, 0x52, 0x53 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_SHRS };

static Abstractformat *xx_shrs_search_open(xx_io_device *window) {
    xx_shrs *reader = xx_shrs_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_shrs_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_shrs_free((xx_shrs *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_shrs_search_open, xx_shrs_search_close
};

static xx_format_search_state *xx_shrs_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_shrs_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_shrs_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_shrs_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_shrs_extractor = {
    xx_shrs_create_format_search,
    xx_shrs_get_current_format_info,
    xx_shrs_format_search_find_next,
    xx_shrs_free_format_search
};
