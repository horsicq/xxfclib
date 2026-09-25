/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_wince_extractor.c - search raw data for Windows CE binary image.
 *
 * Scans for:
 *   42 30 30 30 46 46 0A at +0  ("B000FF.")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the wince reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/wince/xx_wince.h"

static const uint8_t k_anchor0[] = { 0x42, 0x30, 0x30, 0x30, 0x46, 0x46, 0x0A };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_WINCE };

static Abstractformat *xx_wince_search_open(xx_io_device *window) {
    xx_wince *reader = xx_wince_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_wince_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_wince_free((xx_wince *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_wince_search_open, xx_wince_search_close
};

static xx_format_search_state *xx_wince_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_wince_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_wince_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_wince_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_wince_extractor = {
    xx_wince_create_format_search,
    xx_wince_get_current_format_info,
    xx_wince_format_search_find_next,
    xx_wince_free_format_search
};
