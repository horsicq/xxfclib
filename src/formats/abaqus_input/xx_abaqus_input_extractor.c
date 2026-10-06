/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_abaqus_input_extractor.c - search raw data for abaqus_input.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the abaqus_input reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/abaqus_input/xx_abaqus_input.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_ABAQUS_INPUT };

static Abstractformat *xx_abaqus_input_search_open(xx_io_device *window) {
    xx_abaqus_input *reader = xx_abaqus_input_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_abaqus_input_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_abaqus_input_free((xx_abaqus_input *)format);
}



static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    NULL, 0U,
    xx_abaqus_input_search_open, xx_abaqus_input_search_close, false
};

static xx_format_search_state *xx_abaqus_input_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_abaqus_input_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_abaqus_input_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_abaqus_input_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_abaqus_input_extractor = {
    xx_abaqus_input_create_format_search,
    xx_abaqus_input_get_current_format_info,
    xx_abaqus_input_format_search_find_next,
    xx_abaqus_input_free_format_search
};
