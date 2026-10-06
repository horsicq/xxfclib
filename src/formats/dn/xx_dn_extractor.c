/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/dn/xx_dn.h"

#ifdef DN
#define DN_EXTRACTOR_TYPE XX_FILE_TYPE_DN
#else
#define DN_EXTRACTOR_TYPE XX_FILE_TYPE_UNKNOWN
#endif

static const uint8_t dn_magic[] = {0x84, 0x8d, 0x01, 0x02};
static const xx_format_search_anchor dn_anchors[] = {
    {dn_magic, sizeof(dn_magic), 0U}
};
static const xx_file_type_t dn_types[] = {DN_EXTRACTOR_TYPE};
static Abstractformat *dn_open(xx_io_device *window) {
    xx_dn *archive = xx_dn_create(window, 0);
    return archive ? &archive->format : NULL;
}
static void dn_close(Abstractformat *format) {
    xx_dn_free((xx_dn *)format);
}
static const xx_format_search_desc dn_desc = {
    dn_types, 1U, dn_anchors, 1U, dn_open, dn_close, false
};
static xx_format_search_state *dn_create_search(xx_format_extractor *self,
    xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&dn_desc, device, options, pd);
}
static const xx_format_search_info *dn_current(xx_format_extractor *self,
                                               xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}
static bool dn_find_next(xx_format_extractor *self,
                         xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void dn_free_search(xx_format_extractor *self,
                           xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_dn_extractor = {
    dn_create_search, dn_current, dn_find_next, dn_free_search
};
