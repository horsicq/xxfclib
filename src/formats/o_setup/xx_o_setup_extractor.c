/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_o_setup_extractor.c - search raw data for O'Setup.
 *
 * Scans for:
 *   4D 5A at +0
 * Each candidate must be accepted by the o_setup reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/o_setup/xx_o_setup.h"

static const uint8_t k_anchor0[] = {0x4D, 0x5A};

static const xx_format_search_anchor k_anchors[] = {
    {k_anchor0, sizeof(k_anchor0), 0U},
};

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_O_SETUP};

static Abstractformat *xx_o_setup_search_open(xx_io_device *window)
{
    xx_o_setup *reader = xx_o_setup_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_o_setup_search_close(Abstractformat *format)
{
    /* The format is the first member, so this is the reader itself. */
    xx_o_setup_free((xx_o_setup *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]), k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]), xx_o_setup_search_open, xx_o_setup_search_close, false};

static xx_format_search_state *xx_o_setup_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_o_setup_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_o_setup_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_o_setup_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_o_setup_extractor = {xx_o_setup_create_format_search, xx_o_setup_get_current_format_info, xx_o_setup_format_search_find_next,
                                            xx_o_setup_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(o_setup, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
