/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_lizard_extractor.c - search raw data for LIZARD.
 *
 * Scans for:
 *   06 22 4D 18 at +0  ("..M.")
 *   50 2A 4D 18 at +0  ("P.M.")
 *   51 2A 4D 18 at +0  ("Q.M.")
 *   52 2A 4D 18 at +0  ("R.M.")
 *   53 2A 4D 18 at +0  ("S.M.")
 *   54 2A 4D 18 at +0  ("T.M.")
 *   55 2A 4D 18 at +0  ("U.M.")
 *   56 2A 4D 18 at +0  ("V.M.")
 *   57 2A 4D 18 at +0  ("W.M.")
 *   58 2A 4D 18 at +0  ("X.M.")
 *   59 2A 4D 18 at +0  ("Y.M.")
 *   5A 2A 4D 18 at +0  ("Z.M.")
 *   5B 2A 4D 18 at +0  ("[.M.")
 *   5C 2A 4D 18 at +0  ("..M.")
 *   5D 2A 4D 18 at +0  ("].M.")
 *   5E 2A 4D 18 at +0  ("^.M.")
 *   5F 2A 4D 18 at +0  ("_.M.")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the lizard reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/lizard/xx_lizard.h"

static const uint8_t k_anchor0[] = { 0x06, 0x22, 0x4D, 0x18 };
static const uint8_t k_anchor1[] = { 0x50, 0x2A, 0x4D, 0x18 };
static const uint8_t k_anchor2[] = { 0x51, 0x2A, 0x4D, 0x18 };
static const uint8_t k_anchor3[] = { 0x52, 0x2A, 0x4D, 0x18 };
static const uint8_t k_anchor4[] = { 0x53, 0x2A, 0x4D, 0x18 };
static const uint8_t k_anchor5[] = { 0x54, 0x2A, 0x4D, 0x18 };
static const uint8_t k_anchor6[] = { 0x55, 0x2A, 0x4D, 0x18 };
static const uint8_t k_anchor7[] = { 0x56, 0x2A, 0x4D, 0x18 };
static const uint8_t k_anchor8[] = { 0x57, 0x2A, 0x4D, 0x18 };
static const uint8_t k_anchor9[] = { 0x58, 0x2A, 0x4D, 0x18 };
static const uint8_t k_anchor10[] = { 0x59, 0x2A, 0x4D, 0x18 };
static const uint8_t k_anchor11[] = { 0x5A, 0x2A, 0x4D, 0x18 };
static const uint8_t k_anchor12[] = { 0x5B, 0x2A, 0x4D, 0x18 };
static const uint8_t k_anchor13[] = { 0x5C, 0x2A, 0x4D, 0x18 };
static const uint8_t k_anchor14[] = { 0x5D, 0x2A, 0x4D, 0x18 };
static const uint8_t k_anchor15[] = { 0x5E, 0x2A, 0x4D, 0x18 };
static const uint8_t k_anchor16[] = { 0x5F, 0x2A, 0x4D, 0x18 };

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
    { k_anchor9, sizeof(k_anchor9), 0U },
    { k_anchor10, sizeof(k_anchor10), 0U },
    { k_anchor11, sizeof(k_anchor11), 0U },
    { k_anchor12, sizeof(k_anchor12), 0U },
    { k_anchor13, sizeof(k_anchor13), 0U },
    { k_anchor14, sizeof(k_anchor14), 0U },
    { k_anchor15, sizeof(k_anchor15), 0U },
    { k_anchor16, sizeof(k_anchor16), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_LIZARD };

static Abstractformat *xx_lizard_search_open(xx_io_device *window) {
    xx_lizard *reader = xx_lizard_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_lizard_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_lizard_free((xx_lizard *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_lizard_search_open, xx_lizard_search_close
};

static xx_format_search_state *xx_lizard_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_lizard_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_lizard_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_lizard_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_lizard_extractor = {
    xx_lizard_create_format_search,
    xx_lizard_get_current_format_info,
    xx_lizard_format_search_find_next,
    xx_lizard_free_format_search
};
