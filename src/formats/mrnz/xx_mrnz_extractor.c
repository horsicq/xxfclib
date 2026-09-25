/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_mrnz_extractor.c - search raw data for MRNZ.
 *
 * Scans for:
 *   4D 52 4E 5A 88 F0 27 33 at +0  ("MRNZ..'3")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the mrnz reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/mrnz/xx_mrnz.h"

static const uint8_t k_anchor0[] = { 0x4D, 0x52, 0x4E, 0x5A, 0x88, 0xF0, 0x27, 0x33 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_MRNZ };

static Abstractformat *xx_mrnz_search_open(xx_io_device *window) {
    xx_mrnz *reader = xx_mrnz_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_mrnz_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_mrnz_free((xx_mrnz *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_mrnz_search_open, xx_mrnz_search_close
};

static xx_format_search_state *xx_mrnz_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_mrnz_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_mrnz_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_mrnz_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_mrnz_extractor = {
    xx_mrnz_create_format_search,
    xx_mrnz_get_current_format_info,
    xx_mrnz_format_search_find_next,
    xx_mrnz_free_format_search
};
