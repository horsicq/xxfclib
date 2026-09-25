/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_spk_extractor.c - search raw data for SPK.
 *
 * Scans for:
 *   1A 81 at +0  ("..")
 *   1A 82 at +0  ("..")
 *   1A 83 at +0  ("..")
 *   1A 84 at +0  ("..")
 *   1A 85 at +0  ("..")
 *   1A 86 at +0  ("..")
 *   1A 87 at +0  ("..")
 *   1A 88 at +0  ("..")
 *   1A 89 at +0  ("..")
 *   1A FF at +0  ("..")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the spk reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/spk/xx_spk.h"

static const uint8_t k_anchor0[] = { 0x1A, 0x81 };
static const uint8_t k_anchor1[] = { 0x1A, 0x82 };
static const uint8_t k_anchor2[] = { 0x1A, 0x83 };
static const uint8_t k_anchor3[] = { 0x1A, 0x84 };
static const uint8_t k_anchor4[] = { 0x1A, 0x85 };
static const uint8_t k_anchor5[] = { 0x1A, 0x86 };
static const uint8_t k_anchor6[] = { 0x1A, 0x87 };
static const uint8_t k_anchor7[] = { 0x1A, 0x88 };
static const uint8_t k_anchor8[] = { 0x1A, 0x89 };
static const uint8_t k_anchor9[] = { 0x1A, 0xFF };

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
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_SPK };

static Abstractformat *xx_spk_search_open(xx_io_device *window) {
    xx_spk *reader = xx_spk_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_spk_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_spk_free((xx_spk *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_spk_search_open, xx_spk_search_close
};

static xx_format_search_state *xx_spk_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_spk_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_spk_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_spk_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_spk_extractor = {
    xx_spk_create_format_search,
    xx_spk_get_current_format_info,
    xx_spk_format_search_find_next,
    xx_spk_free_format_search
};
