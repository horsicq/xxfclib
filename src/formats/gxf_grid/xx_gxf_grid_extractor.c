/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_gxf_grid_extractor.c - search raw data for gxf_grid.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the gxf_grid reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/gxf_grid/xx_gxf_grid.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_GXF_GRID };

static Abstractformat *xx_gxf_grid_search_open(xx_io_device *window) {
    xx_gxf_grid *reader = xx_gxf_grid_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_gxf_grid_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_gxf_grid_free((xx_gxf_grid *)format);
}



static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    NULL, 0U,
    xx_gxf_grid_search_open, xx_gxf_grid_search_close
};

static xx_format_search_state *xx_gxf_grid_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_gxf_grid_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_gxf_grid_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_gxf_grid_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_gxf_grid_extractor = {
    xx_gxf_grid_create_format_search,
    xx_gxf_grid_get_current_format_info,
    xx_gxf_grid_format_search_find_next,
    xx_gxf_grid_free_format_search
};
