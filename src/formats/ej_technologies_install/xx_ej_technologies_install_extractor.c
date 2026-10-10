/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search raw data for ej-technologies install4j / exe4j.
 * The reader validates and measures each candidate, and the detector must
 * name the same format on the candidate view. Offset 0 is always tried.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/ej_technologies_install/xx_ej_technologies_install.h"

static const uint8_t k_anchor0[] = {0x4D, 0x5A};
static const xx_format_search_anchor k_anchors[] = {
    {k_anchor0, sizeof(k_anchor0), 0U},
};

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_EJ_TECHNOLOGIES_INSTALL};

static Abstractformat *xx_ej_technologies_install_search_open(xx_io_device *window)
{
    xx_ej_technologies_install *reader = xx_ej_technologies_install_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_ej_technologies_install_search_close(Abstractformat *format)
{
    xx_ej_technologies_install_free((xx_ej_technologies_install *)format);
}

static const xx_format_search_desc k_desc = {k_types,
                                             sizeof(k_types) / sizeof(k_types[0]),
                                             k_anchors,
                                             sizeof(k_anchors) / sizeof(k_anchors[0]),
                                             xx_ej_technologies_install_search_open,
                                             xx_ej_technologies_install_search_close,
                                             false};

static xx_format_search_state *xx_ej_technologies_install_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
                                                                               xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_ej_technologies_install_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_ej_technologies_install_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_ej_technologies_install_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_ej_technologies_install_extractor = {xx_ej_technologies_install_create_format_search, xx_ej_technologies_install_get_current_format_info,
                                                            xx_ej_technologies_install_format_search_find_next, xx_ej_technologies_install_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(ej_technologies_install, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
