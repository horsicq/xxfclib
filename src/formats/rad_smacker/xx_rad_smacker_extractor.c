/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_rad_smacker_extractor.c - search raw data for rad_smacker.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the rad_smacker reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/rad_smacker/xx_rad_smacker.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_RAD_SMACKER };

static Abstractformat *xx_rad_smacker_search_open(xx_io_device *window) {
    xx_rad_smacker *reader = xx_rad_smacker_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_rad_smacker_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_rad_smacker_free((xx_rad_smacker *)format);
}

static const uint8_t anchor_0[] = {0x53,0x4d,0x4b,0x32};
static const uint8_t anchor_1[] = {0x53,0x4d,0x4b,0x34};
static const xx_format_search_anchor anchors[] = { { anchor_0,sizeof(anchor_0),0 },{ anchor_1,sizeof(anchor_1),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 2U,
    xx_rad_smacker_search_open, xx_rad_smacker_search_close, false
};

static xx_format_search_state *xx_rad_smacker_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_rad_smacker_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_rad_smacker_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_rad_smacker_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_rad_smacker_extractor = {
    xx_rad_smacker_create_format_search,
    xx_rad_smacker_get_current_format_info,
    xx_rad_smacker_format_search_find_next,
    xx_rad_smacker_free_format_search
};
