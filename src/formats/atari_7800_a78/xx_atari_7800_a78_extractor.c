/* SPDX-License-Identifier: MIT. Validated original-component search. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/atari_7800_a78/xx_atari_7800_a78.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_ATARI_7800_A78};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_atari_7800_a78 *r = xx_atari_7800_a78_create(d, 0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f)
{
    xx_atari_7800_a78_free((xx_atari_7800_a78 *)f);
}
static const uint8_t bytes_0[] = {0x41, 0x54, 0x41, 0x52, 0x49, 0x37, 0x38, 0x30, 0x30};
static const xx_format_search_anchor anchors[] = {{bytes_0, sizeof(bytes_0), 1}};
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
xx_format_extractor xx_atari_7800_a78_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(atari_7800_a78, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
