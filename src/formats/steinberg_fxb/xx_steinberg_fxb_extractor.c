/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/lsp-plugins/lsp-3rd-party/master/include/steinberg/vst2.h
 * Big-endian VST2 CcnK FxCk/FPCh presets and FxBk/FBCh banks, bank versions1/2 and presetversion1. Up to1024 programs/65536 normalized finite parameters and256MiB opaque
 * plugin data. Validates signed chunk sizes, identities, program counts/current program and exact nesting. Exports descriptors, parameter arrays or declared opaque
 * plugin state without interpreting/loading plugins. A standalone FPCh zero outer byteSize is accepted only with a complete signed inner chunk length, as emitted by
 * Surge XT. Unknown versions/extensions rejected.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/steinberg_fxb/xx_steinberg_fxb.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_STEINBERG_FXB};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_steinberg_fxb *r = xx_steinberg_fxb_create(d, 0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f)
{
    xx_steinberg_fxb_free((xx_steinberg_fxb *)f);
}
static const uint8_t bytes_0[] = {0x43, 0x63, 0x6e, 0x4b};
static const xx_format_search_anchor anchors[] = {{bytes_0, sizeof(bytes_0), 0}};
static const xx_format_search_desc desc = {types, 1U, anchors, sizeof(anchors) / sizeof(anchors[0]), open_reader, close_reader, false};
static xx_format_search_state *create_search(xx_format_extractor *x, xx_io_device *d, const xx_list_s *o, xx_pd_struct *pd)
{
    (void)x;
    return xx_format_search_create(&desc, d, o, pd);
}
static const xx_format_search_info *current_search(xx_format_extractor *x, xx_format_search_state *s)
{
    (void)x;
    return xx_format_search_current(s);
}
static bool next_search(xx_format_extractor *x, xx_format_search_state *s, xx_pd_struct *pd)
{
    (void)x;
    return xx_format_search_find_next(s, pd);
}
static void free_search(xx_format_extractor *x, xx_format_search_state *s)
{
    (void)x;
    xx_format_search_free(s);
}
xx_format_extractor xx_steinberg_fxb_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(steinberg_fxb, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
