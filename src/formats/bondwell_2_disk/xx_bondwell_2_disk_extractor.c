/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search candidates are validated by both the reader and the detector.
 * Formats without a fixed signature are considered at offset zero only.
 * See ../xx_format_extractor_engine.h.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/bondwell_2_disk/xx_bondwell_2_disk.h"


static const xx_file_type_t k_types[] = { XX_FILE_TYPE_BONDWELL_2_DISK };

static Abstractformat *xx_bondwell_2_disk_search_open(xx_io_device *window) {
    xx_bondwell_2_disk *reader = xx_bondwell_2_disk_create(window, 0);
    return reader ? &reader->format : NULL;
}
static void xx_bondwell_2_disk_search_close(Abstractformat *format) {
    xx_bondwell_2_disk_free((xx_bondwell_2_disk *)format);
}
static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    NULL, 0U,
    xx_bondwell_2_disk_search_open, xx_bondwell_2_disk_search_close
};
static xx_format_search_state *xx_bondwell_2_disk_search_create(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}
static const xx_format_search_info *xx_bondwell_2_disk_search_current(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}
static bool xx_bondwell_2_disk_search_next(xx_format_extractor *self,
    xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void xx_bondwell_2_disk_search_free(xx_format_extractor *self,
    xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_bondwell_2_disk_extractor = {
    xx_bondwell_2_disk_search_create, xx_bondwell_2_disk_search_current,
    xx_bondwell_2_disk_search_next, xx_bondwell_2_disk_search_free
};

