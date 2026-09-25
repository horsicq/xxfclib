/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_tplink_extractor.c - search raw data for TP-Link firmware.
 *
 * Scans for:
 *   54 50 2D 4C 49 4E 4B 20 54 65 63 68 6E 6F 6C 6F 67 69 65 73 at +4  ("TP-LINK Technologies")
 *   00 14 2F C0 at +0  ("....")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the tplink reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/tplink/xx_tplink.h"

static const uint8_t k_anchor0[] = { 0x54, 0x50, 0x2D, 0x4C, 0x49, 0x4E, 0x4B, 0x20, 0x54, 0x65, 0x63, 0x68, 0x6E, 0x6F, 0x6C, 0x6F, 0x67, 0x69, 0x65, 0x73 };
static const uint8_t k_anchor1[] = { 0x00, 0x14, 0x2F, 0xC0 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 4U },
    { k_anchor1, sizeof(k_anchor1), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_TPLINK };

static Abstractformat *xx_tplink_search_open(xx_io_device *window) {
    xx_tplink *reader = xx_tplink_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_tplink_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_tplink_free((xx_tplink *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_tplink_search_open, xx_tplink_search_close
};

static xx_format_search_state *xx_tplink_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_tplink_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_tplink_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_tplink_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_tplink_extractor = {
    xx_tplink_create_format_search,
    xx_tplink_get_current_format_info,
    xx_tplink_format_search_find_next,
    xx_tplink_free_format_search
};
