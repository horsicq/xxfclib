/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_aldus_extractor.c - search raw data for ALDUS.
 *
 * Scans for:
 *   41 4C 44 55 53 20 4C 5A at +0  ("ALDUS LZ")
 *   41 4C 44 55 53 20 50 4B at +0  ("ALDUS PK")
 *   41 44 4F 42 45 20 4C 5A at +0  ("ADOBE LZ")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the aldus reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/aldus/xx_aldus.h"

static const uint8_t k_anchor0[] = { 0x41, 0x4C, 0x44, 0x55, 0x53, 0x20, 0x4C, 0x5A };
static const uint8_t k_anchor1[] = { 0x41, 0x4C, 0x44, 0x55, 0x53, 0x20, 0x50, 0x4B };
static const uint8_t k_anchor2[] = { 0x41, 0x44, 0x4F, 0x42, 0x45, 0x20, 0x4C, 0x5A };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
    { k_anchor1, sizeof(k_anchor1), 0U },
    { k_anchor2, sizeof(k_anchor2), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_ALDUS };

static Abstractformat *xx_aldus_search_open(xx_io_device *window) {
    xx_aldus *reader = xx_aldus_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_aldus_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_aldus_free((xx_aldus *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_aldus_search_open, xx_aldus_search_close
};

static xx_format_search_state *xx_aldus_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_aldus_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_aldus_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_aldus_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_aldus_extractor = {
    xx_aldus_create_format_search,
    xx_aldus_get_current_format_info,
    xx_aldus_format_search_find_next,
    xx_aldus_free_format_search
};
