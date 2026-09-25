/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_gksetup_extractor.c - search raw data for GkSetup.
 *
 * Scans for:
 *   54 68 69 73 20 69 73 20 61 20 62 69 6E 61 72 79 20 64 61 74 61 20 66 69 6C 65 2E 20 4B 65 65 70 20 6F 75 74 20 21 1A at +0  ("This is a binary data file. Keep out !.")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the gksetup reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/gksetup/xx_gksetup.h"

static const uint8_t k_anchor0[] = { 0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x61, 0x20, 0x62, 0x69, 0x6E, 0x61, 0x72, 0x79, 0x20, 0x64, 0x61, 0x74, 0x61, 0x20, 0x66, 0x69, 0x6C, 0x65, 0x2E, 0x20, 0x4B, 0x65, 0x65, 0x70, 0x20, 0x6F, 0x75, 0x74, 0x20, 0x21, 0x1A };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_GKSETUP };

static Abstractformat *xx_gksetup_search_open(xx_io_device *window) {
    xx_gksetup *reader = xx_gksetup_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_gksetup_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_gksetup_free((xx_gksetup *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_gksetup_search_open, xx_gksetup_search_close
};

static xx_format_search_state *xx_gksetup_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_gksetup_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_gksetup_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_gksetup_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_gksetup_extractor = {
    xx_gksetup_create_format_search,
    xx_gksetup_get_current_format_info,
    xx_gksetup_format_search_find_next,
    xx_gksetup_free_format_search
};
