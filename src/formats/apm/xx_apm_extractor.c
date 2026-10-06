/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_apm_extractor.c - search raw data for Apple Partition Map.
 *
 * Scans for:
 *   45 52 02 00 at +0  ("ER", sbBlkSize 512)
 *   45 52 04 00 at +0  ("ER", sbBlkSize 1024)
 *   45 52 08 00 at +0  ("ER", sbBlkSize 2048)
 *   45 52 10 00 at +0  ("ER", sbBlkSize 4096)
 * Each candidate must be accepted by the apm reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/apm/xx_apm.h"

static const uint8_t k_anchor0[] = { 0x45, 0x52, 0x02, 0x00 };
static const uint8_t k_anchor1[] = { 0x45, 0x52, 0x04, 0x00 };
static const uint8_t k_anchor2[] = { 0x45, 0x52, 0x08, 0x00 };
static const uint8_t k_anchor3[] = { 0x45, 0x52, 0x10, 0x00 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
    { k_anchor1, sizeof(k_anchor1), 0U },
    { k_anchor2, sizeof(k_anchor2), 0U },
    { k_anchor3, sizeof(k_anchor3), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_APM };

static Abstractformat *xx_apm_search_open(xx_io_device *window) {
    xx_apm *reader = xx_apm_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_apm_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_apm_free((xx_apm *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_apm_search_open, xx_apm_search_close, false
};

static xx_format_search_state *xx_apm_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_apm_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_apm_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_apm_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_apm_extractor = {
    xx_apm_create_format_search,
    xx_apm_get_current_format_info,
    xx_apm_format_search_find_next,
    xx_apm_free_format_search
};
