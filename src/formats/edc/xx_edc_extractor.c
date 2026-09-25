/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_edc_extractor.c - search raw data for EDC.
 *
 * Scans for:
 *   20 45 44 43 20 50 61 63 6B 65 64 20 at +0  (" EDC Packed ")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the edc reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/edc/xx_edc.h"

static const uint8_t k_anchor0[] = { 0x20, 0x45, 0x44, 0x43, 0x20, 0x50, 0x61, 0x63, 0x6B, 0x65, 0x64, 0x20 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_EDC };

static Abstractformat *xx_edc_search_open(xx_io_device *window) {
    xx_edc *reader = xx_edc_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_edc_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_edc_free((xx_edc *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_edc_search_open, xx_edc_search_close
};

static xx_format_search_state *xx_edc_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_edc_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_edc_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_edc_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_edc_extractor = {
    xx_edc_create_format_search,
    xx_edc_get_current_format_info,
    xx_edc_format_search_find_next,
    xx_edc_free_format_search
};
