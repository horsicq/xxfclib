/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_sw_extractor.c - search raw data for SW.
 *
 * Scans for:
 *   69 6D 30 30 31 56 at +0  ("im001V")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the sw reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/sw/xx_sw.h"

static const uint8_t k_anchor0[] = { 0x69, 0x6D, 0x30, 0x30, 0x31, 0x56 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_SW };

static Abstractformat *xx_sw_search_open(xx_io_device *window) {
    xx_sw *reader = xx_sw_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_sw_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_sw_free((xx_sw *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_sw_search_open, xx_sw_search_close
};

static xx_format_search_state *xx_sw_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_sw_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_sw_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_sw_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_sw_extractor = {
    xx_sw_create_format_search,
    xx_sw_get_current_format_info,
    xx_sw_format_search_find_next,
    xx_sw_free_format_search
};
