/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_eschalon_setup_epsf_extractor.c - search raw data for eschalon_setup_epsf.
 *
 * Scans for:
 *   4D 5A at +0  ("MZ")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the eschalon_setup_epsf reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/eschalon_setup_epsf/xx_eschalon_setup_epsf.h"

static const uint8_t k_anchor0[] = { 0x4D, 0x5A };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_ESCHALON_SETUP_EPSF };

static Abstractformat *xx_eschalon_setup_epsf_search_open(xx_io_device *window) {
    xx_eschalon_setup_epsf *reader = xx_eschalon_setup_epsf_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_eschalon_setup_epsf_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_eschalon_setup_epsf_free((xx_eschalon_setup_epsf *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_eschalon_setup_epsf_search_open, xx_eschalon_setup_epsf_search_close
};

static xx_format_search_state *xx_eschalon_setup_epsf_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_eschalon_setup_epsf_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_eschalon_setup_epsf_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_eschalon_setup_epsf_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_eschalon_setup_epsf_extractor = {
    xx_eschalon_setup_epsf_create_format_search,
    xx_eschalon_setup_epsf_get_current_format_info,
    xx_eschalon_setup_epsf_format_search_find_next,
    xx_eschalon_setup_epsf_free_format_search
};
