/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_sfx_flashjester_jugglor_extractor.c - search raw data for sfx_flashjester_jugglor.
 *
 * Scans for:
 *   4D 5A at +0  ("MZ")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the sfx_flashjester_jugglor reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/sfx_flashjester_jugglor/xx_sfx_flashjester_jugglor.h"

static const uint8_t k_anchor0[] = {0x4D, 0x5A};

static const xx_format_search_anchor k_anchors[] = {
    {k_anchor0, sizeof(k_anchor0), 0U},
};

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_SFX_FLASHJESTER_JUGGLOR};

static Abstractformat *xx_sfx_flashjester_jugglor_search_open(xx_io_device *window)
{
    xx_sfx_flashjester_jugglor *reader = xx_sfx_flashjester_jugglor_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_sfx_flashjester_jugglor_search_close(Abstractformat *format)
{
    /* The format is the first member, so this is the reader itself. */
    xx_sfx_flashjester_jugglor_free((xx_sfx_flashjester_jugglor *)format);
}

static const xx_format_search_desc k_desc = {k_types,
                                             sizeof(k_types) / sizeof(k_types[0]),
                                             k_anchors,
                                             sizeof(k_anchors) / sizeof(k_anchors[0]),
                                             xx_sfx_flashjester_jugglor_search_open,
                                             xx_sfx_flashjester_jugglor_search_close,
                                             false};

static xx_format_search_state *xx_sfx_flashjester_jugglor_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
                                                                               xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_sfx_flashjester_jugglor_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_sfx_flashjester_jugglor_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_sfx_flashjester_jugglor_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_sfx_flashjester_jugglor_extractor = {xx_sfx_flashjester_jugglor_create_format_search, xx_sfx_flashjester_jugglor_get_current_format_info,
                                                            xx_sfx_flashjester_jugglor_format_search_find_next, xx_sfx_flashjester_jugglor_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(sfx_flashjester_jugglor, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
