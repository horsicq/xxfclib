/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search candidates are validated by both the reader and the detector.
 * Formats without a fixed signature are considered at offset zero only.
 * See ../xx_format_extractor_engine.h.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/pc_magazine_flp/xx_pc_magazine_flp.h"

static const uint8_t k_anchor0[] = { 0x50, 0x43, 0x4D };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_PC_MAGAZINE_FLP };

static Abstractformat *xx_pc_magazine_flp_search_open(xx_io_device *window) {
    xx_pc_magazine_flp *reader = xx_pc_magazine_flp_create(window, 0);
    return reader ? &reader->format : NULL;
}
static void xx_pc_magazine_flp_search_close(Abstractformat *format) {
    xx_pc_magazine_flp_free((xx_pc_magazine_flp *)format);
}
static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_pc_magazine_flp_search_open, xx_pc_magazine_flp_search_close
};
static xx_format_search_state *xx_pc_magazine_flp_search_create(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}
static const xx_format_search_info *xx_pc_magazine_flp_search_current(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}
static bool xx_pc_magazine_flp_search_next(xx_format_extractor *self,
    xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void xx_pc_magazine_flp_search_free(xx_format_extractor *self,
    xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_pc_magazine_flp_extractor = {
    xx_pc_magazine_flp_search_create, xx_pc_magazine_flp_search_current,
    xx_pc_magazine_flp_search_next, xx_pc_magazine_flp_search_free
};

