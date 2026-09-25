/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_is7inx_extractor.c - search raw data for IS7INX.
 *
 * Scans for:
 *   74 C4 2C 84 at +0  ("t.,.")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the is7inx reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/is7inx/xx_is7inx.h"

static const uint8_t k_anchor0[] = { 0x74, 0xC4, 0x2C, 0x84 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_IS7INX };

static Abstractformat *xx_is7inx_search_open(xx_io_device *window) {
    xx_is7inx *reader = xx_is7inx_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_is7inx_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_is7inx_free((xx_is7inx *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_is7inx_search_open, xx_is7inx_search_close
};

static xx_format_search_state *xx_is7inx_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_is7inx_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_is7inx_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_is7inx_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_is7inx_extractor = {
    xx_is7inx_create_format_search,
    xx_is7inx_get_current_format_info,
    xx_is7inx_format_search_find_next,
    xx_is7inx_free_format_search
};
