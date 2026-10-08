/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search candidates are validated by both the reader and the detector.
 * Formats without a fixed signature are considered at offset zero only.
 * See ../xx_format_extractor_engine.h.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/qemu_enhanced_disk/xx_qemu_enhanced_disk.h"

static const uint8_t k_anchor0[] = { 0x51, 0x45, 0x44, 0x00 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_QEMU_ENHANCED_DISK };

static Abstractformat *xx_qemu_enhanced_disk_search_open(xx_io_device *window) {
    xx_qemu_enhanced_disk *reader = xx_qemu_enhanced_disk_create(window, 0);
    return reader ? &reader->format : NULL;
}
static void xx_qemu_enhanced_disk_search_close(Abstractformat *format) {
    xx_qemu_enhanced_disk_free((xx_qemu_enhanced_disk *)format);
}
static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_qemu_enhanced_disk_search_open, xx_qemu_enhanced_disk_search_close, false
};
static xx_format_search_state *xx_qemu_enhanced_disk_search_create(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}
static const xx_format_search_info *xx_qemu_enhanced_disk_search_current(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}
static bool xx_qemu_enhanced_disk_search_next(xx_format_extractor *self,
    xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void xx_qemu_enhanced_disk_search_free(xx_format_extractor *self,
    xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_qemu_enhanced_disk_extractor = {
    xx_qemu_enhanced_disk_search_create, xx_qemu_enhanced_disk_search_current,
    xx_qemu_enhanced_disk_search_next, xx_qemu_enhanced_disk_search_free
};


/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(qemu_enhanced_disk, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
