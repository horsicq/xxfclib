/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_wordperfect_wpg_extractor.c - search raw data for wordperfect_wpg.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the wordperfect_wpg reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/wordperfect_wpg/xx_wordperfect_wpg.h"

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_WORDPERFECT_WPG};

static Abstractformat *xx_wordperfect_wpg_search_open(xx_io_device *window)
{
    xx_wordperfect_wpg *reader = xx_wordperfect_wpg_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_wordperfect_wpg_search_close(Abstractformat *format)
{
    /* The format is the first member, so this is the reader itself. */
    xx_wordperfect_wpg_free((xx_wordperfect_wpg *)format);
}

static const uint8_t anchor_bytes[] = {0xff, 0x57, 0x50, 0x43};
static const xx_format_search_anchor anchors[] = {{anchor_bytes, sizeof(anchor_bytes), 0}};

static const xx_format_search_desc k_desc = {k_types, sizeof(k_types) / sizeof(k_types[0]), anchors, 1U, xx_wordperfect_wpg_search_open, xx_wordperfect_wpg_search_close,
                                             false};

static xx_format_search_state *xx_wordperfect_wpg_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_wordperfect_wpg_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_wordperfect_wpg_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_wordperfect_wpg_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_wordperfect_wpg_extractor = {xx_wordperfect_wpg_create_format_search, xx_wordperfect_wpg_get_current_format_info,
                                                    xx_wordperfect_wpg_format_search_find_next, xx_wordperfect_wpg_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(wordperfect_wpg, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
