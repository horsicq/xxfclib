/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_nrg_extractor.c - raw-data search for Nero NRG disc image.
 *
 * This format has no fixed start signature. Search probes offset zero only;
 * the reader validates its structure and the detector must confirm the type.
 * See xx_format_extractor_engine.h for the shared bounded search engine.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/nrg/xx_nrg.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_NRG };

static Abstractformat *xx_nrg_search_open(xx_io_device *window) {
    xx_nrg *reader = xx_nrg_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_nrg_search_close(Abstractformat *format) {
    xx_nrg_free((xx_nrg *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    NULL, 0U,
    xx_nrg_search_open, xx_nrg_search_close, false
};

static xx_format_search_state *xx_nrg_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_nrg_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_nrg_format_search_find_next(
    xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_nrg_free_format_search(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_nrg_extractor = {
    xx_nrg_create_format_search,
    xx_nrg_get_current_format_info,
    xx_nrg_format_search_find_next,
    xx_nrg_free_format_search
};


/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(nrg, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
