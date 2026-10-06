/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_dds_extractor.c - search raw data for dds.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the dds reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/dds/xx_dds.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_DDS };

static Abstractformat *xx_dds_search_open(xx_io_device *window) {
    xx_dds *reader = xx_dds_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_dds_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_dds_free((xx_dds *)format);
}

static const uint8_t anchor_bytes[] = {0x44,0x44,0x53,0x20};
static const xx_format_search_anchor anchors[] = { { anchor_bytes,sizeof(anchor_bytes),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_dds_search_open, xx_dds_search_close, false
};

static xx_format_search_state *xx_dds_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_dds_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_dds_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_dds_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_dds_extractor = {
    xx_dds_create_format_search,
    xx_dds_get_current_format_info,
    xx_dds_format_search_find_next,
    xx_dds_free_format_search
};
