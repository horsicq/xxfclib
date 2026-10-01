/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search candidates are validated by both the reader and the detector.
 * Formats without a fixed signature are considered at offset zero only.
 * See ../xx_format_extractor_engine.h.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/vdi/xx_vdi.h"

static const uint8_t k_anchor0[] = { 0x7F, 0x10, 0xDA, 0xBE };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 64U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_VDI };

static Abstractformat *xx_vdi_search_open(xx_io_device *window) {
    xx_vdi *reader = xx_vdi_create(window, 0);
    return reader ? &reader->format : NULL;
}
static void xx_vdi_search_close(Abstractformat *format) {
    xx_vdi_free((xx_vdi *)format);
}
static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_vdi_search_open, xx_vdi_search_close
};
static xx_format_search_state *xx_vdi_search_create(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}
static const xx_format_search_info *xx_vdi_search_current(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}
static bool xx_vdi_search_next(xx_format_extractor *self,
    xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void xx_vdi_search_free(xx_format_extractor *self,
    xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_vdi_extractor = {
    xx_vdi_search_create, xx_vdi_search_current,
    xx_vdi_search_next, xx_vdi_search_free
};

