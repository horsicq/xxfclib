/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_crx_extractor.c - search raw data for crx.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the crx reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/crx/xx_crx.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_CRX };

static Abstractformat *xx_crx_search_open(xx_io_device *window) {
    xx_crx *reader = xx_crx_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_crx_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_crx_free((xx_crx *)format);
}

static const uint8_t anchor_0[] = {0x43,0x72,0x32,0x34};
static const xx_format_search_anchor anchors[] = {
    { anchor_0,sizeof(anchor_0),0 },
};

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, sizeof(anchors)/sizeof(anchors[0]),
    xx_crx_search_open, xx_crx_search_close
};

static xx_format_search_state *xx_crx_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_crx_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_crx_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_crx_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_crx_extractor = {
    xx_crx_create_format_search,
    xx_crx_get_current_format_info,
    xx_crx_format_search_find_next,
    xx_crx_free_format_search
};
