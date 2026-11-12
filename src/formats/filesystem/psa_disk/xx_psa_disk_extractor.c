/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * PSA_DISK: the legacy format search of the psa_disk reader, so that the format is
 * a family of its own (XFU_SUPPORT_FORMAT_PSA_DISK). The search descriptor and
 * the abstract binding live in xx_psa_disk.c; this file only publishes the
 * legacy xx_format_extractor over the same descriptor.
 */
#include "../../xx_format_extractor_engine.h"
#include "xxfclib/formats/filesystem/psa_disk/xx_psa_disk.h"
extern const xx_format_search_desc xx_psa_disk_search_desc;
static xx_format_search_state *create_search(xx_format_extractor *x, xx_io_device *d, const xx_list_s *o, xx_pd_struct *pd)
{
    (void)x;
    return xx_format_search_create(&xx_psa_disk_search_desc, d, o, pd);
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
xx_format_extractor xx_psa_disk_extractor = {create_search, current_search, next_search, free_search};
