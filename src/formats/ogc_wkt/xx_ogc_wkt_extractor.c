/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_ogc_wkt_extractor.c - search raw data for ogc_wkt.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the ogc_wkt reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/ogc_wkt/xx_ogc_wkt.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_OGC_WKT };

static Abstractformat *xx_ogc_wkt_search_open(xx_io_device *window) {
    xx_ogc_wkt *reader = xx_ogc_wkt_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_ogc_wkt_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_ogc_wkt_free((xx_ogc_wkt *)format);
}



static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    NULL, 0U,
    xx_ogc_wkt_search_open, xx_ogc_wkt_search_close
};

static xx_format_search_state *xx_ogc_wkt_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_ogc_wkt_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_ogc_wkt_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_ogc_wkt_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_ogc_wkt_extractor = {
    xx_ogc_wkt_create_format_search,
    xx_ogc_wkt_get_current_format_info,
    xx_ogc_wkt_format_search_find_next,
    xx_ogc_wkt_free_format_search
};
