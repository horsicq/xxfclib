/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_arcfs_extractor.c - search raw data for ARCFS.
 *
 * Scans for:
 *   41 72 63 68 69 76 65 00 at +0  ("Archive.")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the arcfs reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/arcfs/xx_arcfs.h"

static const uint8_t k_anchor0[] = { 0x41, 0x72, 0x63, 0x68, 0x69, 0x76, 0x65, 0x00 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_ARCFS };

static Abstractformat *xx_arcfs_search_open(xx_io_device *window) {
    xx_arcfs *reader = xx_arcfs_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_arcfs_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_arcfs_free((xx_arcfs *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_arcfs_search_open, xx_arcfs_search_close
};

static xx_format_search_state *xx_arcfs_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_arcfs_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_arcfs_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_arcfs_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_arcfs_extractor = {
    xx_arcfs_create_format_search,
    xx_arcfs_get_current_format_info,
    xx_arcfs_format_search_find_next,
    xx_arcfs_free_format_search
};
