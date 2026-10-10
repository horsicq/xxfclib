/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_idtech_roq_extractor.c - search raw data for idtech_roq.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the idtech_roq reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/idtech_roq/xx_idtech_roq.h"

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_IDTECH_ROQ};

static Abstractformat *xx_idtech_roq_search_open(xx_io_device *window)
{
    xx_idtech_roq *reader = xx_idtech_roq_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_idtech_roq_search_close(Abstractformat *format)
{
    /* The format is the first member, so this is the reader itself. */
    xx_idtech_roq_free((xx_idtech_roq *)format);
}

static const uint8_t anchor_0[] = {0x84, 0x10, 0xff, 0xff, 0xff, 0xff};
static const xx_format_search_anchor anchors[] = {{anchor_0, sizeof(anchor_0), 0}};

static const xx_format_search_desc k_desc = {k_types, sizeof(k_types) / sizeof(k_types[0]), anchors, 1U, xx_idtech_roq_search_open, xx_idtech_roq_search_close, false};

static xx_format_search_state *xx_idtech_roq_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_idtech_roq_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_idtech_roq_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_idtech_roq_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_idtech_roq_extractor = {xx_idtech_roq_create_format_search, xx_idtech_roq_get_current_format_info, xx_idtech_roq_format_search_find_next,
                                               xx_idtech_roq_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(idtech_roq, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
