/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_genius_extractor.c - search raw data for GENIUS.
 *
 * Scans for:
 *   47 45 4E 49 55 53 20 4C 49 42 52 41 52 59 00 at +0  ("GENIUS LIBRARY.")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the genius reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/genius/xx_genius.h"

static const uint8_t k_anchor0[] = { 0x47, 0x45, 0x4E, 0x49, 0x55, 0x53, 0x20, 0x4C, 0x49, 0x42, 0x52, 0x41, 0x52, 0x59, 0x00 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_GENIUS };

static Abstractformat *xx_genius_search_open(xx_io_device *window) {
    xx_genius *reader = xx_genius_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_genius_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_genius_free((xx_genius *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_genius_search_open, xx_genius_search_close
};

static xx_format_search_state *xx_genius_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_genius_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_genius_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_genius_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_genius_extractor = {
    xx_genius_create_format_search,
    xx_genius_get_current_format_info,
    xx_genius_format_search_find_next,
    xx_genius_free_format_search
};
