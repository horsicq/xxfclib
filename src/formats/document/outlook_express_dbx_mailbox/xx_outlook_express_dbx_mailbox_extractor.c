/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Outlook Express Dbx Mailbox: the format search of the outlook_express_dbx_mailbox reader, so that the format is a
 * family of its own (XFU_SUPPORT_FORMAT_OUTLOOK_EXPRESS_DBX_MAILBOX) and not only a row of the
 * complete library.
 */
#include "../../xx_format_extractor_engine.h"
#include "xxfclib/formats/document/outlook_express_dbx_mailbox/xx_outlook_express_dbx_mailbox.h"
static const xx_file_type_t types[] = {XX_FILE_TYPE_OUTLOOK_EXPRESS_DBX_MAILBOX};
static Abstractformat *open_reader(xx_io_device *d)
{
    Abstractformat *format = (Abstractformat *)xx_outlook_express_dbx_mailbox_create(d, 0);
    if (format && format->file_type == XX_FILE_TYPE_UNKNOWN) format->file_type = XX_FILE_TYPE_OUTLOOK_EXPRESS_DBX_MAILBOX;
    return format;
}
static void close_reader(Abstractformat *f)
{
    xx_outlook_express_dbx_mailbox_free((xx_outlook_express_dbx_mailbox *)f);
}
static const xx_format_search_desc desc = {types, 1U, NULL, 0U, open_reader, close_reader, true};
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
xx_format_extractor xx_outlook_express_dbx_mailbox_extractor = {create_search, current_search, next_search, free_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(outlook_express_dbx_mailbox, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
