/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_linuxzimage_extractor.c - raw-data search for Microsoft LINUX_ZIMAGE.
 *
 * Candidates are nominated by a fixed signature; the reader validates the
 * structure and the detector must confirm the type before a find is returned.
 * See xx_format_extractor_engine.h for the shared bounded search engine.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/linuxzimage/xx_linuxzimage.h"

static const uint8_t k_anchor0[] = {0x18, 0x28, 0x6F, 0x01};
static const uint8_t k_anchor1[] = {0x01, 0x6F, 0x28, 0x18};

static const xx_format_search_anchor k_anchors[] = {
    {k_anchor0, sizeof(k_anchor0), 36U},
    {k_anchor1, sizeof(k_anchor1), 36U},
};

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_LINUX_ZIMAGE};

static Abstractformat *xx_linuxzimage_search_open(xx_io_device *window)
{
    xx_linuxzimage *reader = xx_linuxzimage_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_linuxzimage_search_close(Abstractformat *format)
{
    xx_linuxzimage_free((xx_linuxzimage *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]), k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]), xx_linuxzimage_search_open, xx_linuxzimage_search_close, false};

static xx_format_search_state *xx_linuxzimage_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_linuxzimage_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_linuxzimage_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_linuxzimage_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_linuxzimage_extractor = {xx_linuxzimage_create_format_search, xx_linuxzimage_get_current_format_info, xx_linuxzimage_format_search_find_next,
                                                xx_linuxzimage_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(linuxzimage, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
