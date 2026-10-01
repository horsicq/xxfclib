/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_font_bdf_extractor.c - search raw data for font_bdf.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the font_bdf reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/font_bdf/xx_font_bdf.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_FONT_BDF };

static Abstractformat *xx_font_bdf_search_open(xx_io_device *window) {
    xx_font_bdf *reader = xx_font_bdf_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_font_bdf_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_font_bdf_free((xx_font_bdf *)format);
}

static const uint8_t anchor_0[] = {0x53,0x54,0x41,0x52,0x54,0x46,0x4f,0x4e,0x54,0x20};
static const xx_format_search_anchor anchors[] = { { anchor_0,sizeof(anchor_0),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_font_bdf_search_open, xx_font_bdf_search_close
};

static xx_format_search_state *xx_font_bdf_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_font_bdf_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_font_bdf_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_font_bdf_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_font_bdf_extractor = {
    xx_font_bdf_create_format_search,
    xx_font_bdf_get_current_format_info,
    xx_font_bdf_format_search_find_next,
    xx_font_bdf_free_format_search
};
