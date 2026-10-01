/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/ka/xx_ka.h"

#ifdef KA
#define KA_EXTRACTOR_TYPE XX_FILE_TYPE_KA
#else
#define KA_EXTRACTOR_TYPE XX_FILE_TYPE_UNKNOWN
#endif

static const uint8_t ka_magic[] = {'K', 'A', ' ', 'A', 'r', 'c', 'h', 'i', 'v', 'e', 0};
static const xx_format_search_anchor ka_anchors[] = {
    {ka_magic, sizeof(ka_magic), 0U}
};
static const xx_file_type_t ka_types[] = {KA_EXTRACTOR_TYPE};
static Abstractformat *ka_open(xx_io_device *window) {
    xx_ka *archive = xx_ka_create(window, 0);
    return archive ? &archive->format : NULL;
}
static void ka_close(Abstractformat *format) {
    xx_ka_free((xx_ka *)format);
}
static const xx_format_search_desc ka_desc = {
    ka_types, 1U, ka_anchors, 1U, ka_open, ka_close
};
static xx_format_search_state *ka_create_search(xx_format_extractor *self,
    xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&ka_desc, device, options, pd);
}
static const xx_format_search_info *ka_current(xx_format_extractor *self,
                                               xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}
static bool ka_find_next(xx_format_extractor *self,
                         xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void ka_free_search(xx_format_extractor *self,
                           xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_ka_extractor = {
    ka_create_search, ka_current, ka_find_next, ka_free_search
};
