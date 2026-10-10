/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/erlang/otp/master/lib/stdlib/src/beam_lib.erl */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/erlang_beam/xx_erlang_beam.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_ERLANG_BEAM};
static Abstractformat *open_reader(xx_io_device *d)
{
    xx_erlang_beam *r = xx_erlang_beam_create(d, 0);
    return r ? &r->format : NULL;
}
static void close_reader(Abstractformat *f)
{
    xx_erlang_beam_free((xx_erlang_beam *)f);
}
static const uint8_t bytes_0[] = {70, 79, 82, 49};
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
xx_format_extractor xx_erlang_beam_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(erlang_beam, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
