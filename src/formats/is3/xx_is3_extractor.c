/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_is3_extractor.c - search raw data for IS3.
 *
 * Scans for:
 *   13 5D 65 8C at +0  (".]e.")
 *   2A AB 79 D8 00 01 00 00 at +0  ("..y.....")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the is3 reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/is3/xx_is3.h"

static const uint8_t k_anchor0[] = { 0x13, 0x5D, 0x65, 0x8C };
static const uint8_t k_anchor1[] = { 0x2A, 0xAB, 0x79, 0xD8, 0x00, 0x01, 0x00, 0x00 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
    { k_anchor1, sizeof(k_anchor1), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_IS3 };

static Abstractformat *xx_is3_search_open(xx_io_device *window) {
    xx_is3 *reader = xx_is3_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_is3_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_is3_free((xx_is3 *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_is3_search_open, xx_is3_search_close
};

static xx_format_search_state *xx_is3_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_is3_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_is3_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_is3_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_is3_extractor = {
    xx_is3_create_format_search,
    xx_is3_get_current_format_info,
    xx_is3_format_search_find_next,
    xx_is3_free_format_search
};
