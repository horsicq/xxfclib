/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_renpy_rpa_extractor.c - search raw data for renpy_rpa.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the renpy_rpa reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/renpy_rpa/xx_renpy_rpa.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_RENPY_RPA };

static Abstractformat *xx_renpy_rpa_search_open(xx_io_device *window) {
    xx_renpy_rpa *reader = xx_renpy_rpa_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_renpy_rpa_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_renpy_rpa_free((xx_renpy_rpa *)format);
}

static const uint8_t anchor_bytes[] = {0x52,0x50,0x41,0x2d,0x33,0x2e,0x30,0x20};
static const xx_format_search_anchor anchors[] = { { anchor_bytes,sizeof(anchor_bytes),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_renpy_rpa_search_open, xx_renpy_rpa_search_close, false
};

static xx_format_search_state *xx_renpy_rpa_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_renpy_rpa_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_renpy_rpa_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_renpy_rpa_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_renpy_rpa_extractor = {
    xx_renpy_rpa_create_format_search,
    xx_renpy_rpa_get_current_format_info,
    xx_renpy_rpa_format_search_find_next,
    xx_renpy_rpa_free_format_search
};
