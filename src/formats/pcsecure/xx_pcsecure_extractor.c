/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_pcsecure_extractor.c - search raw data for PCSECURE.
 *
 * Scans for:
 *   50 43 54 35 at +0  ("PCT5")
 *   50 43 54 36 at +0  ("PCT6")
 *   50 43 54 37 at +0  ("PCT7")
 *   41 66 6F 53 at +0  ("AfoS")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the pcsecure reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/pcsecure/xx_pcsecure.h"

static const uint8_t k_anchor0[] = { 0x50, 0x43, 0x54, 0x35 };
static const uint8_t k_anchor1[] = { 0x50, 0x43, 0x54, 0x36 };
static const uint8_t k_anchor2[] = { 0x50, 0x43, 0x54, 0x37 };
static const uint8_t k_anchor3[] = { 0x41, 0x66, 0x6F, 0x53 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
    { k_anchor1, sizeof(k_anchor1), 0U },
    { k_anchor2, sizeof(k_anchor2), 0U },
    { k_anchor3, sizeof(k_anchor3), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_PCSECURE };

static Abstractformat *xx_pcsecure_search_open(xx_io_device *window) {
    xx_pcsecure *reader = xx_pcsecure_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_pcsecure_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_pcsecure_free((xx_pcsecure *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_pcsecure_search_open, xx_pcsecure_search_close
};

static xx_format_search_state *xx_pcsecure_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_pcsecure_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_pcsecure_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_pcsecure_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_pcsecure_extractor = {
    xx_pcsecure_create_format_search,
    xx_pcsecure_get_current_format_info,
    xx_pcsecure_format_search_find_next,
    xx_pcsecure_free_format_search
};
