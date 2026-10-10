/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_packit_mac_extractor.c - raw-data search for Macintosh PackIt.
 *
 * Candidates are nominated by a fixed signature; the reader validates the
 * structure and the detector must confirm the type before a find is returned.
 * See xx_format_extractor_engine.h for the shared bounded search engine.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/packit_mac/xx_packit_mac.h"

static const uint8_t k_anchor0[] = {0x50, 0x4D, 0x61, 0x67};
static const uint8_t k_anchor1[] = {0x50, 0x4D, 0x61, 0x34};

static const xx_format_search_anchor k_anchors[] = {
    {k_anchor0, sizeof(k_anchor0), 0U},
    {k_anchor1, sizeof(k_anchor1), 0U},
};

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_PACKIT_MAC};

static Abstractformat *xx_packit_mac_search_open(xx_io_device *window)
{
    xx_packit_mac *reader = xx_packit_mac_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_packit_mac_search_close(Abstractformat *format)
{
    xx_packit_mac_free((xx_packit_mac *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]), k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]), xx_packit_mac_search_open, xx_packit_mac_search_close, false};

static xx_format_search_state *xx_packit_mac_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_packit_mac_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_packit_mac_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_packit_mac_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_packit_mac_extractor = {xx_packit_mac_create_format_search, xx_packit_mac_get_current_format_info, xx_packit_mac_format_search_find_next,
                                               xx_packit_mac_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(packit_mac, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
