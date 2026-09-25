/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_sfpack_extractor.c - search raw data for SFPack.
 *
 * Scans for:
 *   53 46 50 4B 00 01 at +0  ("SFPK..")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the sfpack reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/sfpack/xx_sfpack.h"

static const uint8_t k_anchor0[] = { 0x53, 0x46, 0x50, 0x4B, 0x00, 0x01 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_SFPACK };

static Abstractformat *xx_sfpack_search_open(xx_io_device *window) {
    xx_sfpack *reader = xx_sfpack_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_sfpack_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_sfpack_free((xx_sfpack *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_sfpack_search_open, xx_sfpack_search_close
};

static xx_format_search_state *xx_sfpack_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_sfpack_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_sfpack_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_sfpack_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_sfpack_extractor = {
    xx_sfpack_create_format_search,
    xx_sfpack_get_current_format_info,
    xx_sfpack_format_search_find_next,
    xx_sfpack_free_format_search
};
