/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_swagpacket_extractor.c - search raw data for SWAGPACKET.
 *
 * Scans for:
 *   53 57 41 47 4F 4C 58 2E 45 58 45 20 28 63 29 20 31 39 39 33 20 47 44 53 4F 46 54 20 20 41 4C 4C 20 52 49 47 48 54 53 20 52 45 53 45 52 56 45 44 at +0  ("SWAGOLX.EXE (c) 1993 GDSOFT  ALL RIGHTS RESERVED")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the swagpacket reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/swagpacket/xx_swagpacket.h"

static const uint8_t k_anchor0[] = { 0x53, 0x57, 0x41, 0x47, 0x4F, 0x4C, 0x58, 0x2E, 0x45, 0x58, 0x45, 0x20, 0x28, 0x63, 0x29, 0x20, 0x31, 0x39, 0x39, 0x33, 0x20, 0x47, 0x44, 0x53, 0x4F, 0x46, 0x54, 0x20, 0x20, 0x41, 0x4C, 0x4C, 0x20, 0x52, 0x49, 0x47, 0x48, 0x54, 0x53, 0x20, 0x52, 0x45, 0x53, 0x45, 0x52, 0x56, 0x45, 0x44 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_SWAGPACKET };

static Abstractformat *xx_swagpacket_search_open(xx_io_device *window) {
    xx_swagpacket *reader = xx_swagpacket_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_swagpacket_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_swagpacket_free((xx_swagpacket *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_swagpacket_search_open, xx_swagpacket_search_close
};

static xx_format_search_state *xx_swagpacket_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_swagpacket_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_swagpacket_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_swagpacket_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_swagpacket_extractor = {
    xx_swagpacket_create_format_search,
    xx_swagpacket_get_current_format_info,
    xx_swagpacket_format_search_find_next,
    xx_swagpacket_free_format_search
};
