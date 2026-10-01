/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/sfx_vms_dcx/xx_sfx_vms_dcx.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_SFX_VMS_DCX};
static const uint8_t image[] = {0xb0, 0x00, 0x30, 0x00};
static const xx_format_search_anchor anchors[] = {
    {image, sizeof(image), 0}
};
static Abstractformat *open_reader(xx_io_device *d) {
    xx_sfx_vms_dcx *r = xx_sfx_vms_dcx_create(d, 0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f) {
    xx_sfx_vms_dcx_free((xx_sfx_vms_dcx *)f);
}
static const xx_format_search_desc desc = {
    types, 1U, anchors, 1U, open_reader, close_reader
};
static xx_format_search_state *create_search(xx_format_extractor *self,
        xx_io_device *d, const xx_list_s *o, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&desc, d, o, pd);
}
static const xx_format_search_info *current(xx_format_extractor *self,
        xx_format_search_state *s) {
    (void)self;
    return xx_format_search_current(s);
}
static bool next(xx_format_extractor *self, xx_format_search_state *s,
                 xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(s, pd);
}
static void free_search(xx_format_extractor *self, xx_format_search_state *s) {
    (void)self;
    xx_format_search_free(s);
}
xx_format_extractor xx_sfx_vms_dcx_extractor = {
    create_search, current, next, free_search
};
