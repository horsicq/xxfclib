/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_infogramesft_extractor.c - search raw data for Infogrames PAK.
 *
 * The format has no fixed signature the detector checks in its first
 * 64 bytes, so it cannot be recognised inside other data: the search
 * tries offset 0 only.
 * Each candidate must be accepted by the infogramesft reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/infogramesft/xx_infogramesft.h"


static const xx_file_type_t k_types[] = { XX_FILE_TYPE_INFOGRAMESFT };

static Abstractformat *xx_infogramesft_search_open(xx_io_device *window) {
    xx_infogramesft *reader = xx_infogramesft_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_infogramesft_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_infogramesft_free((xx_infogramesft *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    NULL, 0U,
    xx_infogramesft_search_open, xx_infogramesft_search_close
};

static xx_format_search_state *xx_infogramesft_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_infogramesft_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_infogramesft_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_infogramesft_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_infogramesft_extractor = {
    xx_infogramesft_create_format_search,
    xx_infogramesft_get_current_format_info,
    xx_infogramesft_format_search_find_next,
    xx_infogramesft_free_format_search
};
