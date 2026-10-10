/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_btoa_extractor.c - search raw data for btoa / Ascii85.
 *
 * Scans for:
 *   78 62 74 6F 61 20 42 65 67 69 6E at +0
 *   78 62 74 6F 61 35 20 at +0
 * Adobe "<~" is deliberately not an anchor: those two bytes occur all over
 * PostScript, PDF and plain text, and an Adobe stream has no checksum.
 * Each candidate must be accepted by the btoa reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/btoa/xx_btoa.h"

static const uint8_t k_anchor0[] = {0x78, 0x62, 0x74, 0x6F, 0x61, 0x20, 0x42, 0x65, 0x67, 0x69, 0x6E};
static const uint8_t k_anchor1[] = {0x78, 0x62, 0x74, 0x6F, 0x61, 0x35, 0x20};

static const xx_format_search_anchor k_anchors[] = {
    {k_anchor0, sizeof(k_anchor0), 0U},
    {k_anchor1, sizeof(k_anchor1), 0U},
};

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_BTOA};

static Abstractformat *xx_btoa_search_open(xx_io_device *window)
{
    xx_btoa *reader = xx_btoa_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_btoa_search_close(Abstractformat *format)
{
    /* The format is the first member, so this is the reader itself. */
    xx_btoa_free((xx_btoa *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]), k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]), xx_btoa_search_open, xx_btoa_search_close, false};

static xx_format_search_state *xx_btoa_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_btoa_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_btoa_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_btoa_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_btoa_extractor = {xx_btoa_create_format_search, xx_btoa_get_current_format_info, xx_btoa_format_search_find_next, xx_btoa_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(btoa, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
