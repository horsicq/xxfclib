/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_rar_extractor.c - search raw data for RAR.
 *
 * Scans for:
 *   52 61 72 21 1A 07 00 at +0  ("Rar!...")
 *   52 61 72 21 1A 07 01 00 at +0  ("Rar!....")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the rar reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/rar/xx_rar.h"

static const uint8_t k_anchor0[] = { 0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x00 };
static const uint8_t k_anchor1[] = { 0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x01, 0x00 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
    { k_anchor1, sizeof(k_anchor1), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_RAR };

static Abstractformat *xx_rar_search_open(xx_io_device *window) {
    xx_rar *reader = xx_rar_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_rar_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_rar_free((xx_rar *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_rar_search_open, xx_rar_search_close
};

static xx_format_search_state *xx_rar_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_rar_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_rar_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_rar_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_rar_extractor = {
    xx_rar_create_format_search,
    xx_rar_get_current_format_info,
    xx_rar_format_search_find_next,
    xx_rar_free_format_search
};
