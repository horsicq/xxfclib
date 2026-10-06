/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search candidates are validated by both the reader and the detector.
 * Formats without a fixed signature are considered at offset zero only.
 * See ../xx_format_extractor_engine.h.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/pchrom/xx_pchrom.h"

static const uint8_t k_anchor0[] = { 0x5A, 0xA5, 0xF0, 0x0F };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 16U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_PCHROM };

static Abstractformat *xx_pchrom_search_open(xx_io_device *window) {
    xx_pchrom *reader = xx_pchrom_create(window, 0);
    return reader ? &reader->format : NULL;
}
static void xx_pchrom_search_close(Abstractformat *format) {
    xx_pchrom_free((xx_pchrom *)format);
}
static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_pchrom_search_open, xx_pchrom_search_close, false
};
static xx_format_search_state *xx_pchrom_search_create(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}
static const xx_format_search_info *xx_pchrom_search_current(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}
static bool xx_pchrom_search_next(xx_format_extractor *self,
    xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void xx_pchrom_search_free(xx_format_extractor *self,
    xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_pchrom_extractor = {
    xx_pchrom_search_create, xx_pchrom_search_current,
    xx_pchrom_search_next, xx_pchrom_search_free
};

