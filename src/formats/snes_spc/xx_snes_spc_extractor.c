/* SPDX-License-Identifier: MIT. Bounded primary-format component search. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/snes_spc/xx_snes_spc.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_SNES_SPC};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_snes_spc *r = xx_snes_spc_create(d, 0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f)
{
    xx_snes_spc_free((xx_snes_spc *)f);
}
static const uint8_t bytes_0[] = {0x53, 0x4e, 0x45, 0x53, 0x2d, 0x53, 0x50, 0x43, 0x37, 0x30, 0x30, 0x20, 0x53, 0x6f, 0x75, 0x6e, 0x64,
                                  0x20, 0x46, 0x69, 0x6c, 0x65, 0x20, 0x44, 0x61, 0x74, 0x61, 0x20, 0x76, 0x30, 0x2e, 0x33, 0x30};
static const xx_format_search_anchor anchors[] = {{bytes_0, sizeof(bytes_0), 0}};
static const xx_format_search_desc desc = {types, 1U, anchors, 1U, open_reader, close_reader, false};
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
xx_format_extractor xx_snes_spc_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(snes_spc, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
