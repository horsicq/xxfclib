/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search Half-Life WAD3 magic, validating the complete member table. */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/halflife_wad3/xx_halflife_wad3.h"

static const uint8_t k_anchor0[] = {0x57, 0x41, 0x44, 0x33};

static const xx_format_search_anchor k_anchors[] = {
    {k_anchor0, sizeof(k_anchor0), 0U},
};

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_HALFLIFE_WAD3};

static Abstractformat *xx_halflife_wad3_search_open(xx_io_device *window)
{
    xx_halflife_wad3 *reader = xx_halflife_wad3_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_halflife_wad3_search_close(Abstractformat *format)
{
    /* The format is the first member, so this is the reader itself. */
    xx_halflife_wad3_free((xx_halflife_wad3 *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]), k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]), xx_halflife_wad3_search_open, xx_halflife_wad3_search_close,
    false};

static xx_format_search_state *xx_halflife_wad3_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_halflife_wad3_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_halflife_wad3_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_halflife_wad3_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_halflife_wad3_extractor = {xx_halflife_wad3_create_format_search, xx_halflife_wad3_get_current_format_info,
                                                  xx_halflife_wad3_format_search_find_next, xx_halflife_wad3_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(halflife_wad3, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
