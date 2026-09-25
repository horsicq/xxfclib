/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_gitobject_extractor.c - search raw data for GIT OBJECT.
 *
 * Scans for:
 *   78 01 at +0  ("x.")
 *   78 5E at +0  ("x^")
 *   78 9C at +0  ("x.")
 *   78 DA at +0  ("x.")
 * (hand-written: the detector establishes this signature outside its
 * 64-byte prefilter window)
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the gitobject reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/gitobject/xx_gitobject.h"

static const uint8_t k_anchor0[] = { 0x78, 0x01 };
static const uint8_t k_anchor1[] = { 0x78, 0x5E };
static const uint8_t k_anchor2[] = { 0x78, 0x9C };
static const uint8_t k_anchor3[] = { 0x78, 0xDA };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
    { k_anchor1, sizeof(k_anchor1), 0U },
    { k_anchor2, sizeof(k_anchor2), 0U },
    { k_anchor3, sizeof(k_anchor3), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_GIT_OBJECT };

static Abstractformat *xx_gitobject_search_open(xx_io_device *window) {
    xx_gitobject *reader = xx_gitobject_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_gitobject_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_gitobject_free((xx_gitobject *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_gitobject_search_open, xx_gitobject_search_close
};

static xx_format_search_state *xx_gitobject_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_gitobject_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_gitobject_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_gitobject_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_gitobject_extractor = {
    xx_gitobject_create_format_search,
    xx_gitobject_get_current_format_info,
    xx_gitobject_format_search_find_next,
    xx_gitobject_free_format_search
};
