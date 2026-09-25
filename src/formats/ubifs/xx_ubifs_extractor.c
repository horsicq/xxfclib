/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_ubifs_extractor.c - search raw data for UBIFS.
 *
 * Scans for:
 *   31 18 10 06 at +0  ("1...")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the ubifs reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/ubifs/xx_ubifs.h"

static const uint8_t k_anchor0[] = { 0x31, 0x18, 0x10, 0x06 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_UBIFS };

static Abstractformat *xx_ubifs_search_open(xx_io_device *window) {
    xx_ubifs *reader = xx_ubifs_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_ubifs_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_ubifs_free((xx_ubifs *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_ubifs_search_open, xx_ubifs_search_close
};

static xx_format_search_state *xx_ubifs_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_ubifs_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_ubifs_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_ubifs_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_ubifs_extractor = {
    xx_ubifs_create_format_search,
    xx_ubifs_get_current_format_info,
    xx_ubifs_format_search_find_next,
    xx_ubifs_free_format_search
};
