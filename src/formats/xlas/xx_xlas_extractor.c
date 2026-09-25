/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_xlas_extractor.c - search raw data for XLAS.
 *
 * Scans for:
 *   58 4C 41 53 at +0  ("XLAS")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the xlas reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/xlas/xx_xlas.h"

static const uint8_t k_anchor0[] = { 0x58, 0x4C, 0x41, 0x53 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_XLAS };

static Abstractformat *xx_xlas_search_open(xx_io_device *window) {
    xx_xlas *reader = xx_xlas_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_xlas_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_xlas_free((xx_xlas *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_xlas_search_open, xx_xlas_search_close
};

static xx_format_search_state *xx_xlas_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_xlas_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_xlas_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_xlas_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_xlas_extractor = {
    xx_xlas_create_format_search,
    xx_xlas_get_current_format_info,
    xx_xlas_format_search_find_next,
    xx_xlas_free_format_search
};
