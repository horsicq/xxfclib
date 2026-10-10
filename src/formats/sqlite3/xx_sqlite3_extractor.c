/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_sqlite3_extractor.c - search raw data for sqlite3.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the sqlite3 reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/sqlite3/xx_sqlite3.h"

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_SQLITE3};

static Abstractformat *xx_sqlite3_search_open(xx_io_device *window)
{
    xx_sqlite3 *reader = xx_sqlite3_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_sqlite3_search_close(Abstractformat *format)
{
    /* The format is the first member, so this is the reader itself. */
    xx_sqlite3_free((xx_sqlite3 *)format);
}

static const uint8_t anchor_0[] = {0x53, 0x51, 0x4c, 0x69, 0x74, 0x65, 0x20, 0x66, 0x6f, 0x72, 0x6d, 0x61, 0x74, 0x20, 0x33, 0x00};
static const xx_format_search_anchor anchors[] = {
    {anchor_0, sizeof(anchor_0), 0},
};

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]), anchors, sizeof(anchors) / sizeof(anchors[0]), xx_sqlite3_search_open, xx_sqlite3_search_close, false};

static xx_format_search_state *xx_sqlite3_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_sqlite3_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_sqlite3_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_sqlite3_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_sqlite3_extractor = {xx_sqlite3_create_format_search, xx_sqlite3_get_current_format_info, xx_sqlite3_format_search_find_next,
                                            xx_sqlite3_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(sqlite3, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
