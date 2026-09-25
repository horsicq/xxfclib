/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_netware2_extractor.c - search raw data for NetWare2.
 *
 * Scans for:
 *   23 00 00 00 10 4E 65 74 57 61 72 65 46 69 6C 65 49 6E 66 6F at +0  ("#....NetWareFileInfo")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the netware2 reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/netware2/xx_netware2.h"

static const uint8_t k_anchor0[] = { 0x23, 0x00, 0x00, 0x00, 0x10, 0x4E, 0x65, 0x74, 0x57, 0x61, 0x72, 0x65, 0x46, 0x69, 0x6C, 0x65, 0x49, 0x6E, 0x66, 0x6F };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_NETWARE2 };

static Abstractformat *xx_netware2_search_open(xx_io_device *window) {
    xx_netware2 *reader = xx_netware2_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_netware2_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_netware2_free((xx_netware2 *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_netware2_search_open, xx_netware2_search_close
};

static xx_format_search_state *xx_netware2_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_netware2_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_netware2_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_netware2_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_netware2_extractor = {
    xx_netware2_create_format_search,
    xx_netware2_get_current_format_info,
    xx_netware2_format_search_find_next,
    xx_netware2_free_format_search
};
