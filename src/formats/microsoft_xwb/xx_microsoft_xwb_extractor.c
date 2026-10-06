/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_microsoft_xwb_extractor.c - search raw data for microsoft_xwb.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the microsoft_xwb reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/microsoft_xwb/xx_microsoft_xwb.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_MICROSOFT_XWB };

static Abstractformat *xx_microsoft_xwb_search_open(xx_io_device *window) {
    xx_microsoft_xwb *reader = xx_microsoft_xwb_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_microsoft_xwb_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_microsoft_xwb_free((xx_microsoft_xwb *)format);
}

static const uint8_t anchor_bytes[] = {0x57,0x42,0x4e,0x44};
static const xx_format_search_anchor anchors[] = { { anchor_bytes,sizeof(anchor_bytes),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_microsoft_xwb_search_open, xx_microsoft_xwb_search_close, false
};

static xx_format_search_state *xx_microsoft_xwb_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_microsoft_xwb_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_microsoft_xwb_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_microsoft_xwb_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_microsoft_xwb_extractor = {
    xx_microsoft_xwb_create_format_search,
    xx_microsoft_xwb_get_current_format_info,
    xx_microsoft_xwb_format_search_find_next,
    xx_microsoft_xwb_free_format_search
};
