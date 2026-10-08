/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search candidates are validated by both the reader and the detector.
 * Formats without a fixed signature are considered at offset zero only.
 * See ../xx_format_extractor_engine.h.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/parallels_hdd/xx_parallels_hdd.h"

static const uint8_t k_anchor0[] = { 0x57, 0x69, 0x74, 0x68, 0x6F, 0x75, 0x74, 0x46, 0x72, 0x65, 0x65, 0x53, 0x70, 0x61, 0x63, 0x65 };
static const uint8_t k_anchor1[] = { 0x57, 0x69, 0x74, 0x68, 0x6F, 0x75, 0x46, 0x72, 0x65, 0x53, 0x70, 0x61, 0x63, 0x45, 0x78, 0x74 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
    { k_anchor1, sizeof(k_anchor1), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_PARALLELS_HDD };

static Abstractformat *xx_parallels_hdd_search_open(xx_io_device *window) {
    xx_parallels_hdd *reader = xx_parallels_hdd_create(window, 0);
    return reader ? &reader->format : NULL;
}
static void xx_parallels_hdd_search_close(Abstractformat *format) {
    xx_parallels_hdd_free((xx_parallels_hdd *)format);
}
static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_parallels_hdd_search_open, xx_parallels_hdd_search_close, false
};
static xx_format_search_state *xx_parallels_hdd_search_create(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}
static const xx_format_search_info *xx_parallels_hdd_search_current(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}
static bool xx_parallels_hdd_search_next(xx_format_extractor *self,
    xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void xx_parallels_hdd_search_free(xx_format_extractor *self,
    xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_parallels_hdd_extractor = {
    xx_parallels_hdd_search_create, xx_parallels_hdd_search_current,
    xx_parallels_hdd_search_next, xx_parallels_hdd_search_free
};


/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(parallels_hdd, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
