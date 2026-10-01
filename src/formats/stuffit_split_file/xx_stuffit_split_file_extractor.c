/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_stuffit_split_file_extractor.c - search raw data for STUFFIT_SPLIT_FILE.
 *
 * The format has no fixed signature the detector checks in its first
 * 64 bytes, so it cannot be recognised inside other data: the search
 * tries offset 0 only.
 * Each candidate must be accepted by the stuffit_split_file reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/stuffit_split_file/xx_stuffit_split_file.h"


static const xx_file_type_t k_types[] = { XX_FILE_TYPE_STUFFIT_SPLIT_FILE };

static Abstractformat *xx_stuffit_split_file_search_open(xx_io_device *window) {
    xx_stuffit_split_file *reader = xx_stuffit_split_file_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_stuffit_split_file_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_stuffit_split_file_free((xx_stuffit_split_file *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    NULL, 0U,
    xx_stuffit_split_file_search_open, xx_stuffit_split_file_search_close
};

static xx_format_search_state *xx_stuffit_split_file_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_stuffit_split_file_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_stuffit_split_file_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_stuffit_split_file_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_stuffit_split_file_extractor = {
    xx_stuffit_split_file_create_format_search,
    xx_stuffit_split_file_get_current_format_info,
    xx_stuffit_split_file_format_search_find_next,
    xx_stuffit_split_file_free_format_search
};
