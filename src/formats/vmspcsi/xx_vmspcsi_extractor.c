/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_vmspcsi_extractor.c - search raw data for VMSPCSI.
 *
 * Scans for:
 *   4F 70 65 6E 56 4D 53 20 44 43 58 20 50 43 53 49 20 43 6F 6D 70 72 65 73 73 65 64 20 46 69 6C 65 at +0  ("OpenVMS DCX PCSI Compressed File")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the vmspcsi reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/vmspcsi/xx_vmspcsi.h"

static const uint8_t k_anchor0[] = { 0x4F, 0x70, 0x65, 0x6E, 0x56, 0x4D, 0x53, 0x20, 0x44, 0x43, 0x58, 0x20, 0x50, 0x43, 0x53, 0x49, 0x20, 0x43, 0x6F, 0x6D, 0x70, 0x72, 0x65, 0x73, 0x73, 0x65, 0x64, 0x20, 0x46, 0x69, 0x6C, 0x65 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_VMSPCSI };

static Abstractformat *xx_vmspcsi_search_open(xx_io_device *window) {
    xx_vmspcsi *reader = xx_vmspcsi_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_vmspcsi_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_vmspcsi_free((xx_vmspcsi *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_vmspcsi_search_open, xx_vmspcsi_search_close
};

static xx_format_search_state *xx_vmspcsi_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_vmspcsi_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_vmspcsi_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_vmspcsi_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_vmspcsi_extractor = {
    xx_vmspcsi_create_format_search,
    xx_vmspcsi_get_current_format_info,
    xx_vmspcsi_format_search_find_next,
    xx_vmspcsi_free_format_search
};
