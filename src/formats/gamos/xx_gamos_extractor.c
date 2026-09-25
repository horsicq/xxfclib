/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_gamos_extractor.c - search raw data for GAMOS.
 *
 * Scans for:
 *   1A 47 41 4D 4F 53 20 50 41 43 4B 45 44 20 46 49 4C 45 at +0  (".GAMOS PACKED FILE")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the gamos reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/gamos/xx_gamos.h"

static const uint8_t k_anchor0[] = { 0x1A, 0x47, 0x41, 0x4D, 0x4F, 0x53, 0x20, 0x50, 0x41, 0x43, 0x4B, 0x45, 0x44, 0x20, 0x46, 0x49, 0x4C, 0x45 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_GAMOS };

static Abstractformat *xx_gamos_search_open(xx_io_device *window) {
    xx_gamos *reader = xx_gamos_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_gamos_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_gamos_free((xx_gamos *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_gamos_search_open, xx_gamos_search_close
};

static xx_format_search_state *xx_gamos_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_gamos_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_gamos_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_gamos_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_gamos_extractor = {
    xx_gamos_create_format_search,
    xx_gamos_get_current_format_info,
    xx_gamos_format_search_find_next,
    xx_gamos_free_format_search
};
