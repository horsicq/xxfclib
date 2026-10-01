/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_openusd_usda_extractor.c - search raw data for openusd_usda.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the openusd_usda reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/openusd_usda/xx_openusd_usda.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_OPENUSD_USDA };

static Abstractformat *xx_openusd_usda_search_open(xx_io_device *window) {
    xx_openusd_usda *reader = xx_openusd_usda_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_openusd_usda_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_openusd_usda_free((xx_openusd_usda *)format);
}

static const uint8_t anchor_bytes[] = {0x23,0x75,0x73,0x64,0x61,0x20,0x31,0x2e,0x30};
static const xx_format_search_anchor anchors[] = { {anchor_bytes,sizeof(anchor_bytes),0} };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_openusd_usda_search_open, xx_openusd_usda_search_close
};

static xx_format_search_state *xx_openusd_usda_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_openusd_usda_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_openusd_usda_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_openusd_usda_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_openusd_usda_extractor = {
    xx_openusd_usda_create_format_search,
    xx_openusd_usda_get_current_format_info,
    xx_openusd_usda_format_search_find_next,
    xx_openusd_usda_free_format_search
};
