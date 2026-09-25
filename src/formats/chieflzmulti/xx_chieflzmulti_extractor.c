/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_chieflzmulti_extractor.c - search raw data for CHIEFLZMULTI.
 *
 * Scans for:
 *   0C 04 0D 43 68 66 4C 5A 5F 32 05 06 04 at +0  ("...ChfLZ_2...")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the chieflzmulti reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/chieflzmulti/xx_chieflzmulti.h"

static const uint8_t k_anchor0[] = { 0x0C, 0x04, 0x0D, 0x43, 0x68, 0x66, 0x4C, 0x5A, 0x5F, 0x32, 0x05, 0x06, 0x04 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_CHIEFLZMULTI };

static Abstractformat *xx_chieflzmulti_search_open(xx_io_device *window) {
    xx_chieflzmulti *reader = xx_chieflzmulti_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_chieflzmulti_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_chieflzmulti_free((xx_chieflzmulti *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_chieflzmulti_search_open, xx_chieflzmulti_search_close
};

static xx_format_search_state *xx_chieflzmulti_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_chieflzmulti_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_chieflzmulti_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_chieflzmulti_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_chieflzmulti_extractor = {
    xx_chieflzmulti_create_format_search,
    xx_chieflzmulti_get_current_format_info,
    xx_chieflzmulti_format_search_find_next,
    xx_chieflzmulti_free_format_search
};
