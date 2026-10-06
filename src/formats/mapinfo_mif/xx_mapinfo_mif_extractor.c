/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_mapinfo_mif_extractor.c - search raw data for mapinfo_mif.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the mapinfo_mif reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/mapinfo_mif/xx_mapinfo_mif.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_MAPINFO_MIF };

static Abstractformat *xx_mapinfo_mif_search_open(xx_io_device *window) {
    xx_mapinfo_mif *reader = xx_mapinfo_mif_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_mapinfo_mif_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_mapinfo_mif_free((xx_mapinfo_mif *)format);
}

static const uint8_t anchor_bytes[] = {0x56,0x65,0x72,0x73,0x69,0x6f,0x6e,0x20};
static const xx_format_search_anchor anchors[] = { {anchor_bytes,sizeof(anchor_bytes),0} };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_mapinfo_mif_search_open, xx_mapinfo_mif_search_close, false
};

static xx_format_search_state *xx_mapinfo_mif_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_mapinfo_mif_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_mapinfo_mif_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_mapinfo_mif_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_mapinfo_mif_extractor = {
    xx_mapinfo_mif_create_format_search,
    xx_mapinfo_mif_get_current_format_info,
    xx_mapinfo_mif_format_search_find_next,
    xx_mapinfo_mif_free_format_search
};
