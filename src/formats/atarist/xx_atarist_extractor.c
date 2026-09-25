/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_atarist_extractor.c - search raw data for ATARIST.
 *
 * The format has no fixed signature the detector checks in its first
 * 64 bytes, so it cannot be recognised inside other data: the search
 * tries offset 0 only.
 * Each candidate must be accepted by the atarist reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/atarist/xx_atarist.h"


static const xx_file_type_t k_types[] = { XX_FILE_TYPE_ATARIST };

static Abstractformat *xx_atarist_search_open(xx_io_device *window) {
    xx_atarist *reader = xx_atarist_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_atarist_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_atarist_free((xx_atarist *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    NULL, 0U,
    xx_atarist_search_open, xx_atarist_search_close
};

static xx_format_search_state *xx_atarist_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_atarist_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_atarist_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_atarist_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_atarist_extractor = {
    xx_atarist_create_format_search,
    xx_atarist_get_current_format_info,
    xx_atarist_format_search_find_next,
    xx_atarist_free_format_search
};
