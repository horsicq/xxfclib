/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_off_mesh_extractor.c - search raw data for off_mesh.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the off_mesh reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/off_mesh/xx_off_mesh.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_OFF_MESH };

static Abstractformat *xx_off_mesh_search_open(xx_io_device *window) {
    xx_off_mesh *reader = xx_off_mesh_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_off_mesh_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_off_mesh_free((xx_off_mesh *)format);
}

static const uint8_t anchor_0[] = {0x4f,0x46,0x46};
static const xx_format_search_anchor anchors[] = { { anchor_0,sizeof(anchor_0),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_off_mesh_search_open, xx_off_mesh_search_close, false
};

static xx_format_search_state *xx_off_mesh_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_off_mesh_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_off_mesh_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_off_mesh_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_off_mesh_extractor = {
    xx_off_mesh_create_format_search,
    xx_off_mesh_get_current_format_info,
    xx_off_mesh_format_search_find_next,
    xx_off_mesh_free_format_search
};
