/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_hog2_extractor.c - search raw data for HOG2.
 *
 * Scans for:
 *   48 4F 47 32 at +0  ("HOG2")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the hog2 reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/hog2/xx_hog2.h"

static const uint8_t k_anchor0[] = { 0x48, 0x4F, 0x47, 0x32 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_HOG2 };

static Abstractformat *xx_hog2_search_open(xx_io_device *window) {
    xx_hog2 *reader = xx_hog2_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_hog2_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_hog2_free((xx_hog2 *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_hog2_search_open, xx_hog2_search_close
};

static xx_format_search_state *xx_hog2_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_hog2_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_hog2_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_hog2_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_hog2_extractor = {
    xx_hog2_create_format_search,
    xx_hog2_get_current_format_info,
    xx_hog2_format_search_find_next,
    xx_hog2_free_format_search
};
