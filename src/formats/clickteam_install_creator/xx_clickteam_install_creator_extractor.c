/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_clickteam_install_creator_extractor.c - search raw data for clickteam_install_creator.
 *
 * Scans for:
 *   4D 5A at +0  ("MZ")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the clickteam_install_creator reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/clickteam_install_creator/xx_clickteam_install_creator.h"

static const uint8_t k_anchor0[] = {0x4D, 0x5A};

static const xx_format_search_anchor k_anchors[] = {
    {k_anchor0, sizeof(k_anchor0), 0U},
};

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_CLICKTEAM_INSTALL_CREATOR};

static Abstractformat *xx_clickteam_install_creator_search_open(xx_io_device *window)
{
    xx_clickteam_install_creator *reader = xx_clickteam_install_creator_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_clickteam_install_creator_search_close(Abstractformat *format)
{
    /* The format is the first member, so this is the reader itself. */
    xx_clickteam_install_creator_free((xx_clickteam_install_creator *)format);
}

static const xx_format_search_desc k_desc = {k_types,
                                             sizeof(k_types) / sizeof(k_types[0]),
                                             k_anchors,
                                             sizeof(k_anchors) / sizeof(k_anchors[0]),
                                             xx_clickteam_install_creator_search_open,
                                             xx_clickteam_install_creator_search_close,
                                             false};

static xx_format_search_state *xx_clickteam_install_creator_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
                                                                                 xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_clickteam_install_creator_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_clickteam_install_creator_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_clickteam_install_creator_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_clickteam_install_creator_extractor = {xx_clickteam_install_creator_create_format_search, xx_clickteam_install_creator_get_current_format_info,
                                                              xx_clickteam_install_creator_format_search_find_next, xx_clickteam_install_creator_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(clickteam_install_creator, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
