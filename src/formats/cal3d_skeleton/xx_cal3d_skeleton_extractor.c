/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_cal3d_skeleton_extractor.c - search raw data for cal3d_skeleton.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the cal3d_skeleton reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/cal3d_skeleton/xx_cal3d_skeleton.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_CAL3D_SKELETON };

static Abstractformat *xx_cal3d_skeleton_search_open(xx_io_device *window) {
    xx_cal3d_skeleton *reader = xx_cal3d_skeleton_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_cal3d_skeleton_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_cal3d_skeleton_free((xx_cal3d_skeleton *)format);
}

static const uint8_t anchor_bytes[] = {0x43,0x53,0x46,0x00};
static const xx_format_search_anchor anchors[] = { {anchor_bytes,sizeof(anchor_bytes),0} };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_cal3d_skeleton_search_open, xx_cal3d_skeleton_search_close, false
};

static xx_format_search_state *xx_cal3d_skeleton_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_cal3d_skeleton_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_cal3d_skeleton_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_cal3d_skeleton_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_cal3d_skeleton_extractor = {
    xx_cal3d_skeleton_create_format_search,
    xx_cal3d_skeleton_get_current_format_info,
    xx_cal3d_skeleton_format_search_find_next,
    xx_cal3d_skeleton_free_format_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(cal3d_skeleton, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
