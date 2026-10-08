/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_alias_pix_extractor.c - search raw data for alias_pix.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the alias_pix reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/alias_pix/xx_alias_pix.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_ALIAS_PIX };

static Abstractformat *xx_alias_pix_search_open(xx_io_device *window) {
    xx_alias_pix *reader = xx_alias_pix_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_alias_pix_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_alias_pix_free((xx_alias_pix *)format);
}



static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    NULL, 0U,
    xx_alias_pix_search_open, xx_alias_pix_search_close, false
};

static xx_format_search_state *xx_alias_pix_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_alias_pix_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_alias_pix_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_alias_pix_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_alias_pix_extractor = {
    xx_alias_pix_create_format_search,
    xx_alias_pix_get_current_format_info,
    xx_alias_pix_format_search_find_next,
    xx_alias_pix_free_format_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(alias_pix, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
