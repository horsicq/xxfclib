/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_autel_extractor.c - search raw data for AUTEL.
 *
 * Scans for:
 *   20 00 00 00 43 6F 70 79 72 69 67 68 74 20 41 75 74 65 6C 00 at +12  (" ...Copyright Autel.")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the autel reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/autel/xx_autel.h"

static const uint8_t k_anchor0[] = { 0x20, 0x00, 0x00, 0x00, 0x43, 0x6F, 0x70, 0x79, 0x72, 0x69, 0x67, 0x68, 0x74, 0x20, 0x41, 0x75, 0x74, 0x65, 0x6C, 0x00 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 12U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_AUTEL };

static Abstractformat *xx_autel_search_open(xx_io_device *window) {
    xx_autel *reader = xx_autel_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_autel_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_autel_free((xx_autel *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_autel_search_open, xx_autel_search_close
};

static xx_format_search_state *xx_autel_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_autel_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_autel_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_autel_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_autel_extractor = {
    xx_autel_create_format_search,
    xx_autel_get_current_format_info,
    xx_autel_format_search_find_next,
    xx_autel_free_format_search
};
