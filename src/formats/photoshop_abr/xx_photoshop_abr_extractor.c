/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_photoshop_abr_extractor.c - search raw data for photoshop_abr.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the photoshop_abr reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/photoshop_abr/xx_photoshop_abr.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_PHOTOSHOP_ABR };

static Abstractformat *xx_photoshop_abr_search_open(xx_io_device *window) {
    xx_photoshop_abr *reader = xx_photoshop_abr_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_photoshop_abr_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_photoshop_abr_free((xx_photoshop_abr *)format);
}

static const uint8_t anchor_0[] = {0x00,0x01};
static const uint8_t anchor_1[] = {0x00,0x02};
static const xx_format_search_anchor anchors[] = { { anchor_0,sizeof(anchor_0),0 },{ anchor_1,sizeof(anchor_1),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 2U,
    xx_photoshop_abr_search_open, xx_photoshop_abr_search_close
};

static xx_format_search_state *xx_photoshop_abr_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_photoshop_abr_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_photoshop_abr_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_photoshop_abr_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_photoshop_abr_extractor = {
    xx_photoshop_abr_create_format_search,
    xx_photoshop_abr_get_current_format_info,
    xx_photoshop_abr_format_search_find_next,
    xx_photoshop_abr_free_format_search
};
