/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_netwarepacked_extractor.c - search raw data for NETWAREPACKED.
 *
 * Scans for:
 *   50 61 63 6B 65 64 20 46 69 6C 65 20 at +0  ("Packed File ")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the netwarepacked reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/netwarepacked/xx_netwarepacked.h"

static const uint8_t k_anchor0[] = { 0x50, 0x61, 0x63, 0x6B, 0x65, 0x64, 0x20, 0x46, 0x69, 0x6C, 0x65, 0x20 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_NETWAREPACKED };

static Abstractformat *xx_netwarepacked_search_open(xx_io_device *window) {
    xx_netwarepacked *reader = xx_netwarepacked_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_netwarepacked_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_netwarepacked_free((xx_netwarepacked *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_netwarepacked_search_open, xx_netwarepacked_search_close
};

static xx_format_search_state *xx_netwarepacked_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_netwarepacked_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_netwarepacked_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_netwarepacked_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_netwarepacked_extractor = {
    xx_netwarepacked_create_format_search,
    xx_netwarepacked_get_current_format_info,
    xx_netwarepacked_format_search_find_next,
    xx_netwarepacked_free_format_search
};
