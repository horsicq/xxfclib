/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_fmc1_extractor.c - search raw data for FMC1.
 *
 * Scans for:
 *   46 4D 43 31 at +0  ("FMC1")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the fmc1 reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/fmc1/xx_fmc1.h"

static const uint8_t k_anchor0[] = { 0x46, 0x4D, 0x43, 0x31 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_FMC1 };

static Abstractformat *xx_fmc1_search_open(xx_io_device *window) {
    xx_fmc1 *reader = xx_fmc1_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_fmc1_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_fmc1_free((xx_fmc1 *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_fmc1_search_open, xx_fmc1_search_close
};

static xx_format_search_state *xx_fmc1_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_fmc1_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_fmc1_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_fmc1_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_fmc1_extractor = {
    xx_fmc1_create_format_search,
    xx_fmc1_get_current_format_info,
    xx_fmc1_format_search_find_next,
    xx_fmc1_free_format_search
};
