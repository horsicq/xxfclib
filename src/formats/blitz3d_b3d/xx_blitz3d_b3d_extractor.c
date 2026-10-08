/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_blitz3d_b3d_extractor.c - search raw data for blitz3d_b3d.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the blitz3d_b3d reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/blitz3d_b3d/xx_blitz3d_b3d.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_BLITZ3D_B3D };

static Abstractformat *xx_blitz3d_b3d_search_open(xx_io_device *window) {
    xx_blitz3d_b3d *reader = xx_blitz3d_b3d_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_blitz3d_b3d_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_blitz3d_b3d_free((xx_blitz3d_b3d *)format);
}

static const uint8_t anchor_bytes[] = {0x42,0x42,0x33,0x44};
static const xx_format_search_anchor anchors[] = { { anchor_bytes,sizeof(anchor_bytes),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_blitz3d_b3d_search_open, xx_blitz3d_b3d_search_close, false
};

static xx_format_search_state *xx_blitz3d_b3d_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_blitz3d_b3d_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_blitz3d_b3d_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_blitz3d_b3d_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_blitz3d_b3d_extractor = {
    xx_blitz3d_b3d_create_format_search,
    xx_blitz3d_b3d_get_current_format_info,
    xx_blitz3d_b3d_format_search_find_next,
    xx_blitz3d_b3d_free_format_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(blitz3d_b3d, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
