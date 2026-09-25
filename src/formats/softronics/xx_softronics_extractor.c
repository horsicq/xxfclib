/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_softronics_extractor.c - search raw data for SOFTRONICS.
 *
 * Scans for:
 *   00 53 6F 66 74 72 6F 6E 69 63 73 20 43 6F 6D 70 72 65 73 73 65 64 20 46 69 6C 65 00 56 65 72 73 69 6F 6E 20 32 2E 30 30 00 at +1  (".Softronics Compressed File.Version 2.00.")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the softronics reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/softronics/xx_softronics.h"

static const uint8_t k_anchor0[] = { 0x00, 0x53, 0x6F, 0x66, 0x74, 0x72, 0x6F, 0x6E, 0x69, 0x63, 0x73, 0x20, 0x43, 0x6F, 0x6D, 0x70, 0x72, 0x65, 0x73, 0x73, 0x65, 0x64, 0x20, 0x46, 0x69, 0x6C, 0x65, 0x00, 0x56, 0x65, 0x72, 0x73, 0x69, 0x6F, 0x6E, 0x20, 0x32, 0x2E, 0x30, 0x30, 0x00 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 1U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_SOFTRONICS };

static Abstractformat *xx_softronics_search_open(xx_io_device *window) {
    xx_softronics *reader = xx_softronics_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_softronics_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_softronics_free((xx_softronics *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_softronics_search_open, xx_softronics_search_close
};

static xx_format_search_state *xx_softronics_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_softronics_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_softronics_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_softronics_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_softronics_extractor = {
    xx_softronics_create_format_search,
    xx_softronics_get_current_format_info,
    xx_softronics_format_search_find_next,
    xx_softronics_free_format_search
};
