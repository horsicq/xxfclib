/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search Doom WAD magic, validating the complete member table. */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/doom_wad/xx_doom_wad.h"

static const uint8_t k_anchor0[] = {0x49, 0x57, 0x41, 0x44};
static const uint8_t k_anchor1[] = {0x50, 0x57, 0x41, 0x44};

static const xx_format_search_anchor k_anchors[] = {
    {k_anchor0, sizeof(k_anchor0), 0U},
    {k_anchor1, sizeof(k_anchor1), 0U},
};

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_DOOM_WAD};

static Abstractformat *xx_doom_wad_search_open(xx_io_device *window)
{
    xx_doom_wad *reader = xx_doom_wad_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_doom_wad_search_close(Abstractformat *format)
{
    /* The format is the first member, so this is the reader itself. */
    xx_doom_wad_free((xx_doom_wad *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]), k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]), xx_doom_wad_search_open, xx_doom_wad_search_close, false};

static xx_format_search_state *xx_doom_wad_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_doom_wad_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_doom_wad_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_doom_wad_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_doom_wad_extractor = {xx_doom_wad_create_format_search, xx_doom_wad_get_current_format_info, xx_doom_wad_format_search_find_next,
                                             xx_doom_wad_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(doom_wad, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
