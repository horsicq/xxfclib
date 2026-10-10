/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_blender_blend_extractor.c - search raw data for blender_blend.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the blender_blend reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/blender_blend/xx_blender_blend.h"

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_BLENDER_BLEND};

static Abstractformat *xx_blender_blend_search_open(xx_io_device *window)
{
    xx_blender_blend *reader = xx_blender_blend_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_blender_blend_search_close(Abstractformat *format)
{
    /* The format is the first member, so this is the reader itself. */
    xx_blender_blend_free((xx_blender_blend *)format);
}

static const uint8_t anchor_bytes[] = {0x42, 0x4c, 0x45, 0x4e, 0x44, 0x45, 0x52};
static const xx_format_search_anchor anchors[] = {{anchor_bytes, sizeof(anchor_bytes), 0}};

static const xx_format_search_desc k_desc = {k_types, sizeof(k_types) / sizeof(k_types[0]), anchors, 1U, xx_blender_blend_search_open, xx_blender_blend_search_close,
                                             false};

static xx_format_search_state *xx_blender_blend_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_blender_blend_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_blender_blend_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_blender_blend_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_blender_blend_extractor = {xx_blender_blend_create_format_search, xx_blender_blend_get_current_format_info,
                                                  xx_blender_blend_format_search_find_next, xx_blender_blend_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(blender_blend, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
