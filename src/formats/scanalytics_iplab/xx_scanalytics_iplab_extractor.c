/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_scanalytics_iplab_extractor.c - search raw data for scanalytics_iplab.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the scanalytics_iplab reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/scanalytics_iplab/xx_scanalytics_iplab.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_SCANALYTICS_IPLAB };

static Abstractformat *xx_scanalytics_iplab_search_open(xx_io_device *window) {
    xx_scanalytics_iplab *reader = xx_scanalytics_iplab_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_scanalytics_iplab_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_scanalytics_iplab_free((xx_scanalytics_iplab *)format);
}

static const uint8_t anchor_bytes[] = {0x69,0x69,0x69,0x69};
static const xx_format_search_anchor anchors[] = { {anchor_bytes,sizeof(anchor_bytes),0} };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_scanalytics_iplab_search_open, xx_scanalytics_iplab_search_close, false
};

static xx_format_search_state *xx_scanalytics_iplab_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_scanalytics_iplab_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_scanalytics_iplab_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_scanalytics_iplab_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_scanalytics_iplab_extractor = {
    xx_scanalytics_iplab_create_format_search,
    xx_scanalytics_iplab_get_current_format_info,
    xx_scanalytics_iplab_format_search_find_next,
    xx_scanalytics_iplab_free_format_search
};
