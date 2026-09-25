/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_apricot_extractor.c - search raw data for Apricot.
 *
 * Scans for:
 *   41 43 54 20 41 70 72 69 63 6F 74 20 64 69 73 6B 20 69 6D 61 67 65 at +0  ("ACT Apricot disk image")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the apricot reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/apricot/xx_apricot.h"

static const uint8_t k_anchor0[] = { 0x41, 0x43, 0x54, 0x20, 0x41, 0x70, 0x72, 0x69, 0x63, 0x6F, 0x74, 0x20, 0x64, 0x69, 0x73, 0x6B, 0x20, 0x69, 0x6D, 0x61, 0x67, 0x65 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_APRICOT };

static Abstractformat *xx_apricot_search_open(xx_io_device *window) {
    xx_apricot *reader = xx_apricot_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_apricot_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_apricot_free((xx_apricot *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_apricot_search_open, xx_apricot_search_close
};

static xx_format_search_state *xx_apricot_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_apricot_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_apricot_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_apricot_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_apricot_extractor = {
    xx_apricot_create_format_search,
    xx_apricot_get_current_format_info,
    xx_apricot_format_search_find_next,
    xx_apricot_free_format_search
};
