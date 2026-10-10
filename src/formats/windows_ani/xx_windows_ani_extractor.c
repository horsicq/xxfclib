/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/windows_ani/xx_windows_ani.h"

static const xx_file_type_t ani_types[] = {XX_FILE_TYPE_WINDOWS_ANI};
static const uint8_t ani_magic[] = {'R', 'I', 'F', 'F'};
static const xx_format_search_anchor ani_anchors[] = {{ani_magic, sizeof(ani_magic), 0U}};

static Abstractformat *ani_search_open(xx_io_device *window)
{
    xx_windows_ani *reader = xx_windows_ani_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void ani_search_close(Abstractformat *format)
{
    xx_windows_ani_free((xx_windows_ani *)format);
}

static const xx_format_search_desc ani_desc = {
    ani_types, sizeof(ani_types) / sizeof(ani_types[0]), ani_anchors, sizeof(ani_anchors) / sizeof(ani_anchors[0]), ani_search_open, ani_search_close, false};

static xx_format_search_state *ani_create_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&ani_desc, device, options, pd);
}

static const xx_format_search_info *ani_current_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool ani_next_search(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void ani_free_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_windows_ani_extractor = {ani_create_search, ani_current_search, ani_next_search, ani_free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(windows_ani, ani_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
