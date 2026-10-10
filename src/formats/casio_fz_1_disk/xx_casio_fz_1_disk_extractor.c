/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search candidates are validated by both the reader and the detector.
 * Formats without a fixed signature are considered at offset zero only.
 * See ../xx_format_extractor_engine.h.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/casio_fz_1_disk/xx_casio_fz_1_disk.h"

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_CASIO_FZ_1_DISK};

static Abstractformat *xx_casio_fz_1_disk_search_open(xx_io_device *window)
{
    xx_casio_fz_1_disk *reader = xx_casio_fz_1_disk_create(window, 0);
    return reader ? &reader->format : NULL;
}
static void xx_casio_fz_1_disk_search_close(Abstractformat *format)
{
    xx_casio_fz_1_disk_free((xx_casio_fz_1_disk *)format);
}
static const xx_format_search_desc k_desc = {k_types, sizeof(k_types) / sizeof(k_types[0]), NULL, 0U, xx_casio_fz_1_disk_search_open, xx_casio_fz_1_disk_search_close,
                                             false};
static xx_format_search_state *xx_casio_fz_1_disk_search_create(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}
static const xx_format_search_info *xx_casio_fz_1_disk_search_current(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}
static bool xx_casio_fz_1_disk_search_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void xx_casio_fz_1_disk_search_free(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_casio_fz_1_disk_extractor = {xx_casio_fz_1_disk_search_create, xx_casio_fz_1_disk_search_current, xx_casio_fz_1_disk_search_next,
                                                    xx_casio_fz_1_disk_search_free};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(casio_fz_1_disk, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
