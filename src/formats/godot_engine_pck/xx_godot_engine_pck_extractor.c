/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_godot_engine_pck_extractor.c - raw-data search for Godot PCK resource pack.
 *
 * Candidates are nominated by a fixed signature; the reader validates the
 * structure and the detector must confirm the type before a find is returned.
 * See xx_format_extractor_engine.h for the shared bounded search engine.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/godot_engine_pck/xx_godot_engine_pck.h"

static const uint8_t k_anchor0[] = {0x47, 0x44, 0x50, 0x43};
static const uint8_t k_anchor1[] = {0x4D, 0x5A};
static const uint8_t k_anchor2[] = {0x7F, 0x45, 0x4C, 0x46};
static const uint8_t k_anchor3[] = {0xFE, 0xED, 0xFA, 0xCE};
static const uint8_t k_anchor4[] = {0xFE, 0xED, 0xFA, 0xCF};
static const uint8_t k_anchor5[] = {0xCE, 0xFA, 0xED, 0xFE};
static const uint8_t k_anchor6[] = {0xCF, 0xFA, 0xED, 0xFE};
static const uint8_t k_anchor7[] = {0xCA, 0xFE, 0xBA, 0xBE};
static const uint8_t k_anchor8[] = {0xCA, 0xFE, 0xBA, 0xBF};
static const uint8_t k_anchor9[] = {0xBE, 0xBA, 0xFE, 0xCA};
static const uint8_t k_anchor10[] = {0xBF, 0xBA, 0xFE, 0xCA};

static const xx_format_search_anchor k_anchors[] = {
    {k_anchor0, sizeof(k_anchor0), 0U}, {k_anchor1, sizeof(k_anchor1), 0U}, {k_anchor2, sizeof(k_anchor2), 0U},   {k_anchor3, sizeof(k_anchor3), 0U},
    {k_anchor4, sizeof(k_anchor4), 0U}, {k_anchor5, sizeof(k_anchor5), 0U}, {k_anchor6, sizeof(k_anchor6), 0U},   {k_anchor7, sizeof(k_anchor7), 0U},
    {k_anchor8, sizeof(k_anchor8), 0U}, {k_anchor9, sizeof(k_anchor9), 0U}, {k_anchor10, sizeof(k_anchor10), 0U},
};

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_GODOT_ENGINE_PCK};

static Abstractformat *xx_godot_engine_pck_search_open(xx_io_device *window)
{
    xx_godot_engine_pck *reader = xx_godot_engine_pck_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_godot_engine_pck_search_close(Abstractformat *format)
{
    xx_godot_engine_pck_free((xx_godot_engine_pck *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]), k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]), xx_godot_engine_pck_search_open, xx_godot_engine_pck_search_close,
    false};

static xx_format_search_state *xx_godot_engine_pck_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_godot_engine_pck_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_godot_engine_pck_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_godot_engine_pck_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_godot_engine_pck_extractor = {xx_godot_engine_pck_create_format_search, xx_godot_engine_pck_get_current_format_info,
                                                     xx_godot_engine_pck_format_search_find_next, xx_godot_engine_pck_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(godot_engine_pck, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
