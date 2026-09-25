/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_ftcomp_extractor.c - search raw data for FTCOMP.
 *
 * Scans for:
 *   46 54 43 4F 4D 50 at +24  ("FTCOMP")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the ftcomp reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/ftcomp/xx_ftcomp.h"

static const uint8_t k_anchor0[] = { 0x46, 0x54, 0x43, 0x4F, 0x4D, 0x50 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 24U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_FTCOMP };

static Abstractformat *xx_ftcomp_search_open(xx_io_device *window) {
    xx_ftcomp *reader = xx_ftcomp_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_ftcomp_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_ftcomp_free((xx_ftcomp *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_ftcomp_search_open, xx_ftcomp_search_close
};

static xx_format_search_state *xx_ftcomp_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_ftcomp_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_ftcomp_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_ftcomp_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_ftcomp_extractor = {
    xx_ftcomp_create_format_search,
    xx_ftcomp_get_current_format_info,
    xx_ftcomp_format_search_find_next,
    xx_ftcomp_free_format_search
};
