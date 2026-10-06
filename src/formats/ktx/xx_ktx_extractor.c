/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_ktx_extractor.c - search raw data for ktx.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the ktx reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/ktx/xx_ktx.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_KTX };

static Abstractformat *xx_ktx_search_open(xx_io_device *window) {
    xx_ktx *reader = xx_ktx_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_ktx_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_ktx_free((xx_ktx *)format);
}

static const uint8_t anchor_bytes[] = {0xab,0x4b,0x54,0x58,0x20,0x31,0x31,0xbb,0x0d,0x0a,0x1a,0x0a};
static const xx_format_search_anchor anchors[] = { { anchor_bytes,sizeof(anchor_bytes),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_ktx_search_open, xx_ktx_search_close, false
};

static xx_format_search_state *xx_ktx_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_ktx_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_ktx_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_ktx_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_ktx_extractor = {
    xx_ktx_create_format_search,
    xx_ktx_get_current_format_info,
    xx_ktx_format_search_find_next,
    xx_ktx_free_format_search
};
