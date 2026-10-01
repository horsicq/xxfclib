/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_stereolithography_stl_extractor.c - search raw data for stereolithography_stl.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the stereolithography_stl reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/stereolithography_stl/xx_stereolithography_stl.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_STEREOLITHOGRAPHY_STL };

static Abstractformat *xx_stereolithography_stl_search_open(xx_io_device *window) {
    xx_stereolithography_stl *reader = xx_stereolithography_stl_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_stereolithography_stl_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_stereolithography_stl_free((xx_stereolithography_stl *)format);
}



static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    NULL, 0U,
    xx_stereolithography_stl_search_open, xx_stereolithography_stl_search_close
};

static xx_format_search_state *xx_stereolithography_stl_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_stereolithography_stl_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_stereolithography_stl_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_stereolithography_stl_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_stereolithography_stl_extractor = {
    xx_stereolithography_stl_create_format_search,
    xx_stereolithography_stl_get_current_format_info,
    xx_stereolithography_stl_format_search_find_next,
    xx_stereolithography_stl_free_format_search
};
