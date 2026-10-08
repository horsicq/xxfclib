/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_trs_80_jv3_extractor.c - search raw data for TRS_80_JV3.
 *
 * The format has no fixed signature the detector checks in its first
 * 64 bytes, so it cannot be recognised inside other data: the search
 * tries offset 0 only.
 * Each candidate must be accepted by the trs_80_jv3 reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/trs_80_jv3/xx_trs_80_jv3.h"


static const xx_file_type_t k_types[] = { XX_FILE_TYPE_TRS_80_JV3 };

static Abstractformat *xx_trs_80_jv3_search_open(xx_io_device *window) {
    xx_trs_80_jv3 *reader = xx_trs_80_jv3_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_trs_80_jv3_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_trs_80_jv3_free((xx_trs_80_jv3 *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    NULL, 0U,
    xx_trs_80_jv3_search_open, xx_trs_80_jv3_search_close, false
};

static xx_format_search_state *xx_trs_80_jv3_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_trs_80_jv3_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_trs_80_jv3_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_trs_80_jv3_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_trs_80_jv3_extractor = {
    xx_trs_80_jv3_create_format_search,
    xx_trs_80_jv3_get_current_format_info,
    xx_trs_80_jv3_format_search_find_next,
    xx_trs_80_jv3_free_format_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(trs_80_jv3, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
