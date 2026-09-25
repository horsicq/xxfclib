/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_lzop_extractor.c - search raw data for LZOP.
 *
 * Scans for:
 *   89 4C 5A 4F 00 0D 0A 1A 0A at +0  (".LZO.....")
 * (hand-written: the detector establishes this signature outside its
 * 64-byte prefilter window)
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the lzop reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/lzop/xx_lzop.h"

static const uint8_t k_anchor0[] = { 0x89, 0x4C, 0x5A, 0x4F, 0x00, 0x0D, 0x0A, 0x1A, 0x0A };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_LZOP };

static Abstractformat *xx_lzop_search_open(xx_io_device *window) {
    xx_lzop *reader = xx_lzop_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_lzop_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_lzop_free((xx_lzop *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_lzop_search_open, xx_lzop_search_close
};

static xx_format_search_state *xx_lzop_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_lzop_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_lzop_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_lzop_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_lzop_extractor = {
    xx_lzop_create_format_search,
    xx_lzop_get_current_format_info,
    xx_lzop_format_search_find_next,
    xx_lzop_free_format_search
};
