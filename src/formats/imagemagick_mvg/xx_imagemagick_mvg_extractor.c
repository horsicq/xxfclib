/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_imagemagick_mvg_extractor.c - search raw data for imagemagick_mvg.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the imagemagick_mvg reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/imagemagick_mvg/xx_imagemagick_mvg.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_IMAGEMAGICK_MVG };

static Abstractformat *xx_imagemagick_mvg_search_open(xx_io_device *window) {
    xx_imagemagick_mvg *reader = xx_imagemagick_mvg_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_imagemagick_mvg_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_imagemagick_mvg_free((xx_imagemagick_mvg *)format);
}

static const uint8_t anchor_bytes[] = {0x70,0x75,0x73,0x68,0x20,0x67,0x72,0x61,0x70,0x68,0x69,0x63,0x2d,0x63,0x6f,0x6e,0x74,0x65,0x78,0x74};
static const xx_format_search_anchor anchors[] = { {anchor_bytes,sizeof(anchor_bytes),0} };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_imagemagick_mvg_search_open, xx_imagemagick_mvg_search_close
};

static xx_format_search_state *xx_imagemagick_mvg_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_imagemagick_mvg_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_imagemagick_mvg_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_imagemagick_mvg_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_imagemagick_mvg_extractor = {
    xx_imagemagick_mvg_create_format_search,
    xx_imagemagick_mvg_get_current_format_info,
    xx_imagemagick_mvg_format_search_find_next,
    xx_imagemagick_mvg_free_format_search
};
