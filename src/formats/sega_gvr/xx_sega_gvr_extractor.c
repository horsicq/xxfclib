/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_sega_gvr_extractor.c - search raw data for sega_gvr.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the sega_gvr reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/sega_gvr/xx_sega_gvr.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_SEGA_GVR };

static Abstractformat *xx_sega_gvr_search_open(xx_io_device *window) {
    xx_sega_gvr *reader = xx_sega_gvr_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_sega_gvr_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_sega_gvr_free((xx_sega_gvr *)format);
}

static const uint8_t anchor_bytes[] = {0x47,0x56,0x52,0x54};
static const xx_format_search_anchor anchors[] = { { anchor_bytes,sizeof(anchor_bytes),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_sega_gvr_search_open, xx_sega_gvr_search_close, false
};

static xx_format_search_state *xx_sega_gvr_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_sega_gvr_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_sega_gvr_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_sega_gvr_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_sega_gvr_extractor = {
    xx_sega_gvr_create_format_search,
    xx_sega_gvr_get_current_format_info,
    xx_sega_gvr_format_search_find_next,
    xx_sega_gvr_free_format_search
};
