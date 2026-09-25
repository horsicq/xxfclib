/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_tarx2_extractor.c - search raw data for TARX2.
 *
 * Scans for:
 *   78 76 95 7D at +0  ("xv.}")
 * (hand-written: the detector establishes this signature outside its
 * 64-byte prefilter window)
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the tarx2 reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/tarx2/xx_tarx2.h"

static const uint8_t k_anchor0[] = { 0x78, 0x76, 0x95, 0x7D };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_TARX2 };

static Abstractformat *xx_tarx2_search_open(xx_io_device *window) {
    xx_tarx2 *reader = xx_tarx2_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_tarx2_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_tarx2_free((xx_tarx2 *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_tarx2_search_open, xx_tarx2_search_close
};

static xx_format_search_state *xx_tarx2_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_tarx2_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_tarx2_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_tarx2_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_tarx2_extractor = {
    xx_tarx2_create_format_search,
    xx_tarx2_get_current_format_info,
    xx_tarx2_format_search_find_next,
    xx_tarx2_free_format_search
};
