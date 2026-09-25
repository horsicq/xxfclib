/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_xz_extractor.c - search raw data for XZ.
 *
 * Scans for:
 *   FD 37 7A 58 5A 00 at +0  (".7zXZ.")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the xz reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/xz/xx_xz.h"

static const uint8_t k_anchor0[] = { 0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_XZ };

static Abstractformat *xx_xz_search_open(xx_io_device *window) {
    xx_xz *reader = xx_xz_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_xz_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_xz_free((xx_xz *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_xz_search_open, xx_xz_search_close
};

static xx_format_search_state *xx_xz_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_xz_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_xz_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_xz_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_xz_extractor = {
    xx_xz_create_format_search,
    xx_xz_get_current_format_info,
    xx_xz_format_search_find_next,
    xx_xz_free_format_search
};
