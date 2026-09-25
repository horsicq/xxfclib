/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_marc_extractor.c - search raw data for MARC.
 *
 * Scans for:
 *   4D 41 52 43 at +0  ("MARC")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the marc reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/marc/xx_marc.h"

static const uint8_t k_anchor0[] = { 0x4D, 0x41, 0x52, 0x43 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_MARC };

static Abstractformat *xx_marc_search_open(xx_io_device *window) {
    xx_marc *reader = xx_marc_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_marc_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_marc_free((xx_marc *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_marc_search_open, xx_marc_search_close
};

static xx_format_search_state *xx_marc_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_marc_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_marc_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_marc_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_marc_extractor = {
    xx_marc_create_format_search,
    xx_marc_get_current_format_info,
    xx_marc_format_search_find_next,
    xx_marc_free_format_search
};
