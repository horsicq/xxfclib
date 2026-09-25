/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_jffs2_extractor.c - search raw data for JFFS2.
 *
 * Scans for:
 *   85 19 at +0  ("..")
 *   19 85 at +0  ("..")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the jffs2 reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/jffs2/xx_jffs2.h"

static const uint8_t k_anchor0[] = { 0x85, 0x19 };
static const uint8_t k_anchor1[] = { 0x19, 0x85 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
    { k_anchor1, sizeof(k_anchor1), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_JFFS2 };

static Abstractformat *xx_jffs2_search_open(xx_io_device *window) {
    xx_jffs2 *reader = xx_jffs2_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_jffs2_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_jffs2_free((xx_jffs2 *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_jffs2_search_open, xx_jffs2_search_close
};

static xx_format_search_state *xx_jffs2_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_jffs2_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_jffs2_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_jffs2_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_jffs2_extractor = {
    xx_jffs2_create_format_search,
    xx_jffs2_get_current_format_info,
    xx_jffs2_format_search_find_next,
    xx_jffs2_free_format_search
};
