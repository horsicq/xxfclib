/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search candidates are validated by both the reader and the detector.
 * Formats without a fixed signature are considered at offset zero only.
 * See ../xx_format_extractor_engine.h.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/rsdos_fs/xx_rsdos_fs.h"


static const xx_file_type_t k_types[] = { XX_FILE_TYPE_RSDOS_FS };

static Abstractformat *xx_rsdos_fs_search_open(xx_io_device *window) {
    xx_rsdos_fs *reader = xx_rsdos_fs_create(window, 0);
    return reader ? &reader->format : NULL;
}
static void xx_rsdos_fs_search_close(Abstractformat *format) {
    xx_rsdos_fs_free((xx_rsdos_fs *)format);
}
static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    NULL, 0U,
    xx_rsdos_fs_search_open, xx_rsdos_fs_search_close
};
static xx_format_search_state *xx_rsdos_fs_search_create(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}
static const xx_format_search_info *xx_rsdos_fs_search_current(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}
static bool xx_rsdos_fs_search_next(xx_format_extractor *self,
    xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void xx_rsdos_fs_search_free(xx_format_extractor *self,
    xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_rsdos_fs_extractor = {
    xx_rsdos_fs_search_create, xx_rsdos_fs_search_current,
    xx_rsdos_fs_search_next, xx_rsdos_fs_search_free
};

