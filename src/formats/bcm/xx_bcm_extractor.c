/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_bcm_extractor.c - search raw data for BCM.
 *
 * Scans for:
 *   42 43 4D 31 at +0  ("BCM1")
 *   42 43 4D 32 at +0  ("BCM2")
 *   42 43 4D 33 at +0  ("BCM3")
 *   42 43 4D 34 at +0  ("BCM4")
 *   42 43 4D 35 at +0  ("BCM5")
 *   42 43 4D 36 at +0  ("BCM6")
 *   42 43 4D 37 at +0  ("BCM7")
 *   42 43 4D 38 at +0  ("BCM8")
 *   42 43 4D 39 at +0  ("BCM9")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the bcm reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/bcm/xx_bcm.h"

static const uint8_t k_anchor0[] = { 0x42, 0x43, 0x4D, 0x31 };
static const uint8_t k_anchor1[] = { 0x42, 0x43, 0x4D, 0x32 };
static const uint8_t k_anchor2[] = { 0x42, 0x43, 0x4D, 0x33 };
static const uint8_t k_anchor3[] = { 0x42, 0x43, 0x4D, 0x34 };
static const uint8_t k_anchor4[] = { 0x42, 0x43, 0x4D, 0x35 };
static const uint8_t k_anchor5[] = { 0x42, 0x43, 0x4D, 0x36 };
static const uint8_t k_anchor6[] = { 0x42, 0x43, 0x4D, 0x37 };
static const uint8_t k_anchor7[] = { 0x42, 0x43, 0x4D, 0x38 };
static const uint8_t k_anchor8[] = { 0x42, 0x43, 0x4D, 0x39 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
    { k_anchor1, sizeof(k_anchor1), 0U },
    { k_anchor2, sizeof(k_anchor2), 0U },
    { k_anchor3, sizeof(k_anchor3), 0U },
    { k_anchor4, sizeof(k_anchor4), 0U },
    { k_anchor5, sizeof(k_anchor5), 0U },
    { k_anchor6, sizeof(k_anchor6), 0U },
    { k_anchor7, sizeof(k_anchor7), 0U },
    { k_anchor8, sizeof(k_anchor8), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_BCM };

static Abstractformat *xx_bcm_search_open(xx_io_device *window) {
    xx_bcm *reader = xx_bcm_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_bcm_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_bcm_free((xx_bcm *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_bcm_search_open, xx_bcm_search_close
};

static xx_format_search_state *xx_bcm_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_bcm_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_bcm_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_bcm_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_bcm_extractor = {
    xx_bcm_create_format_search,
    xx_bcm_get_current_format_info,
    xx_bcm_format_search_find_next,
    xx_bcm_free_format_search
};
