/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_unifont_hex_extractor.c - search raw data for unifont_hex.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the unifont_hex reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/unifont_hex/xx_unifont_hex.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_UNIFONT_HEX };

static Abstractformat *xx_unifont_hex_search_open(xx_io_device *window) {
    xx_unifont_hex *reader = xx_unifont_hex_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_unifont_hex_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_unifont_hex_free((xx_unifont_hex *)format);
}



static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    NULL, 0U,
    xx_unifont_hex_search_open, xx_unifont_hex_search_close, false
};

static xx_format_search_state *xx_unifont_hex_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_unifont_hex_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_unifont_hex_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_unifont_hex_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_unifont_hex_extractor = {
    xx_unifont_hex_create_format_search,
    xx_unifont_hex_get_current_format_info,
    xx_unifont_hex_format_search_find_next,
    xx_unifont_hex_free_format_search
};
