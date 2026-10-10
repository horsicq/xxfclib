/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_base64_extractor.c - search raw data for Base64.
 *
 * No invariant prefix identifies this format's start, so only offset 0
 * is tried. Footer-located and headerless streams still use their reader's
 * full structural validation.
 * Each candidate must be accepted by the base64 reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/base64/xx_base64.h"

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_BASE64};

static Abstractformat *xx_base64_search_open(xx_io_device *window)
{
    xx_base64 *reader = xx_base64_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_base64_search_close(Abstractformat *format)
{
    /* The format is the first member, so this is the reader itself. */
    xx_base64_free((xx_base64 *)format);
}

static const xx_format_search_desc k_desc = {k_types, sizeof(k_types) / sizeof(k_types[0]), NULL, 0U, xx_base64_search_open, xx_base64_search_close, false};

static xx_format_search_state *xx_base64_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_base64_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_base64_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_base64_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_base64_extractor = {xx_base64_create_format_search, xx_base64_get_current_format_info, xx_base64_format_search_find_next,
                                           xx_base64_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(base64, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
