/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_nastran_bulk_extractor.c - search raw data for nastran_bulk.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the nastran_bulk reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/nastran_bulk/xx_nastran_bulk.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_NASTRAN_BULK };

static Abstractformat *xx_nastran_bulk_search_open(xx_io_device *window) {
    xx_nastran_bulk *reader = xx_nastran_bulk_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_nastran_bulk_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_nastran_bulk_free((xx_nastran_bulk *)format);
}



static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    NULL, 0U,
    xx_nastran_bulk_search_open, xx_nastran_bulk_search_close, false
};

static xx_format_search_state *xx_nastran_bulk_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_nastran_bulk_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_nastran_bulk_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_nastran_bulk_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_nastran_bulk_extractor = {
    xx_nastran_bulk_create_format_search,
    xx_nastran_bulk_get_current_format_info,
    xx_nastran_bulk_format_search_find_next,
    xx_nastran_bulk_free_format_search
};
